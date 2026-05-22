// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#pragma once

#include "gen.h"
#include <ArduinoJson.h>
#include <array>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <stdint.h>
#include <unordered_map>
#include <vector>


class breakpoint;
class bus;
class mmu;

constexpr const int      max_stacktrace_depth = 16;
constexpr const uint16_t B16_MSBSET = 0x8000;
constexpr const uint32_t B32_MSBSET = 0x80000000;
constexpr const uint32_t B32_MSWSET = 0xffff0000;
constexpr const uint64_t B64_MSWSET = 0xffffffff00000000ll;

typedef struct {
	word_mode_t    word_mode;
	d_i_space_t    space;

	bool           is_addr;
	union {
		uint16_t       addr;
		int            reg;
	};

	uint16_t       value;
} gam_rc_t;

class cpu
{
private:
	uint16_t regs0_5[2][6]; // R0...5, selected by bit 11 in PSW, 
	uint16_t sp[3 + 1]; // stackpointers, MF../MT.. select via 12/13 from PSW, others via 14/15
	uint16_t pc                 { 0     };
	uint16_t instruction_start  { 0     };
	uint16_t psw                { 0     };
	uint16_t fpsr               { 0     };
	uint16_t stack_limit_register { 0400 };
	int      processing_trap_depth { 0  };
	bool     it_is_a_trap       { false };
	std::optional<int> delayed_trap {   };  // invoked after completion of the instruction
	bool     debug_mode         { false };
	std::vector<std::pair<uint16_t, std::string> > stacktrace;
	int      kw11l_counter      { 0     };
	bool     wait_stuck         { false };
	uint64_t trap_counter       { 0     };
	std::unordered_map<uint16_t, uint32_t> trap_counts;

	// vector, 8 levels
	std::array<std::set<uint16_t>, 8> queued_interrupts;
	abool        any_queued_interrupts { false };
#if defined(FREERTOS)
	SemaphoreHandle_t       qi_lock { xSemaphoreCreateBinary() };
	QueueHandle_t           qi_q    { xQueueCreate(16, 1)      };
#else
	std::mutex              qi_lock;
	std::condition_variable qi_cv;
#endif

	std::unordered_map<int, breakpoint *> breakpoints;
	int                     bp_nr       { 0 };

	bus *const b    { nullptr };
	mmu *const mmu_ { nullptr };

	kek_event_t *const event { nullptr };
	console     *cnsl        { nullptr };

	bool     check_pending_interrupts() const;  // needs the 'qi_lock'-lock
	void     execute_any_pending_interrupt();

	uint32_t shifter(uint32_t value, int shift, bool is32b);

	uint16_t add_register(const int nr, const uint16_t value);

	void     add_to_MMR1(const int reg, const int delta);

	gam_rc_t getGAM(const uint8_t mode, const uint8_t reg, const word_mode_t word_mode, const bool read_value = true);
	gam_rc_t getGAMAddress(const uint8_t mode, const int reg, const word_mode_t word_mode);
	bool     putGAM(const gam_rc_t & g, const uint16_t value); // returns false when flag registers should not be updated

	std::optional<bool> conditional_branch_instructions_evaluate(const uint16_t instr) const;
	bool double_operand_instructions(const uint16_t instr);
	bool additional_double_operand_instructions(const uint16_t instr);
	bool single_operand_instructions(const uint16_t instr);
	bool conditional_branch_instructions(const uint16_t instr);
	bool condition_code_operations(const uint16_t instr);
	bool misc_operations(const uint16_t instr);

	struct operand_parameters {
		std::string operand;
		int         length;
		int         instruction_part;
		uint16_t    work_value;
		bool        valid;
		std::optional<std::string> error;
	};

	operand_parameters addressing_to_string(const uint8_t mode_register, const uint16_t pc, const word_mode_t word_mode) const;
	uint16_t peek_dst(const int mode, const int reg, const uint16_t pc, const word_mode_t word_mode) const;

	void add_to_stack_trace(const uint16_t p);
	void pop_from_stack_trace();

public:
	explicit cpu(bus *const b, kek_event_t *const event);
	~cpu();

	void set_console(console *const cnsl) { this->cnsl = cnsl; }

	JsonDocument serialize();
	static cpu *deserialize(const JsonVariantConst j, bus *const b, kek_event_t *const event);

	std::optional<std::pair<breakpoint &, const std::string> > check_breakpoint();
	int                         set_breakpoint(breakpoint *const bp);
	bool                        remove_breakpoint(const int bp_id);
	std::unordered_map<int, breakpoint *> list_breakpoints();

	void disassemble(void) const;
	std::unordered_map<std::string, std::vector<std::string> > disassemble(const uint16_t addr) const;

	bus *getBus() { return b; }

	bool     get_debug() const { return debug_mode; }
	void     set_debug(const bool d) { debug_mode = d; stacktrace.clear(); }
	std::vector<std::pair<uint16_t, std::string> > get_stack_trace() const;

	void     reset();
	bool     step ();

	uint32_t calc_instruction_duration(const uint16_t pc) const;  // nanoseconds
	uint64_t get_trap_counter() const { return trap_counter; }
	auto     get_trap_counts() const { return trap_counts; }

	void     push_stack(const uint16_t v);
	uint16_t pop_stack();

	void init_interrupt_queue();
	void queue_interrupt(const uint8_t level, const uint16_t vector);
	void unqueue_interrupt(const uint8_t level, const uint16_t vector);
	std::array<std::set<uint16_t>, 8> get_queued_interrupts() const { return queued_interrupts; }
	bool check_if_interrupts_pending() const { return any_queued_interrupts; }

	void trap(uint16_t vector, const int new_ipl = -1, const bool is_interrupt = false);
	bool is_it_a_trap() const { return it_is_a_trap; }

	bool getPSW_c() const;
	bool getPSW_v() const;
	bool getPSW_z() const;
	bool getPSW_n() const;
	int  getPSW_spl() const;
	bool getBitPSW(const int bit) const;
	int  getPSW_runmode() const { return psw >> 14; };
	int  getPSW_prev_runmode() const { return (psw >> 12) & 3; };
	bool get_register_set() const { return !!(psw & 04000); }

	void setPSW_c(const bool v);
	void setPSW_v(const bool v);
	void setPSW_z(const bool v);
	void setPSW_n(const bool v);
	void setPSW_spl(const int v);
	void setBitPSW(const int bit, const bool v);
	void setPSW_flags_nzv(const uint16_t value, const word_mode_t word_mode);

	uint16_t getPSW() const { return psw; }
	void     setPSW(const uint16_t v, const bool limited);

	uint16_t get_stack_limit_register() { return stack_limit_register; }
	void     set_stack_limit_register(const uint16_t v) { stack_limit_register = v; }

	uint16_t get_stackpointer(const int which) const { assert(which >= 0 && which < 4); return sp[which]; }
	uint16_t getPC() const { return pc; }
	void set_stackpointer(const int which, const uint16_t value) { assert(which >= 0 && which < 4); sp[which] = value; }
	void setPC(const uint16_t value) { pc = value; }

	void set_register(const int nr, const uint16_t value);
	void set_registerLowByte(const int nr, const word_mode_t word_mode, const uint16_t value);
	// used by 'main' for json-validation
	void lowlevel_register_set(const uint8_t set, const uint8_t reg, const uint16_t value);
	void lowlevel_register_sp_set(const uint8_t set, const uint16_t value);
	uint16_t lowlevel_register_get(const uint8_t set, const uint8_t reg) const;
	void lowlevel_psw_set(const uint16_t value) { psw = value; }
	uint16_t lowlevel_register_sp_get(const uint8_t nr) const { return sp[nr]; }

	uint16_t  get_register        (const int nr) const;
	uint16_t *get_register_pointer(const int nr);

	bool put_result(const gam_rc_t & g, const uint16_t value);
};
