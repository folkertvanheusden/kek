// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include "gen.h"
#include <ArduinoJson.h>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdint.h>
#include <vector>


class breakpoint;
class bus;

constexpr const int      initial_trap_delay   = 8;
constexpr const int      max_stacktrace_depth = 16;
constexpr const uint32_t B32_MSBSET = 0x80000000;
constexpr const uint64_t B64_MSWSET = 0xffffffff00000000ll;

typedef struct {
	int      delta;
	unsigned reg;
} mmr1_delta_t;

typedef struct {
	word_mode_t    word_mode;
	rm_selection_t mode_selection;
	d_i_space_t    space;
	int            access_mode;

	// for MMR1 register
	std::optional<mmr1_delta_t> mmr1_update;

	std::optional<uint16_t> addr;
	std::optional<int>      reg;

	std::optional<uint16_t> value;
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
	uint16_t stackLimitRegister { 0377  };
	int      processing_trap_depth { 0  };
	uint64_t instruction_count  { 0     };
	uint64_t running_since      { 0     };
	uint64_t wait_time          { 0     };
	bool     it_is_a_trap       { false };
	std::optional<int> trap_delay { 0   };
	bool     debug_mode         { false };
	std::vector<std::pair<uint16_t, std::string> > stacktrace;

	// level, vector
	std::map<uint8_t, std::set<uint8_t> > queued_interrupts;
	std::atomic_bool        any_queued_interrupts { false };
#if defined(BUILD_FOR_RP2040)
	SemaphoreHandle_t       qi_lock { xSemaphoreCreateBinary() };
	QueueHandle_t           qi_q    { xQueueCreate(16, 1)      };
#else
	std::mutex              qi_lock;
	std::condition_variable qi_cv;
#endif

	std::map<int, breakpoint *> breakpoints;
	int                         bp_nr       { 0 };

	bus *const b { nullptr };

	std::atomic_uint32_t *const event { nullptr };

	bool     check_pending_interrupts() const;  // needs the 'qi_lock'-lock
	bool     execute_any_pending_interrupt();

	uint16_t add_register(const int nr, const uint16_t value);

	void     addToMMR1(const gam_rc_t & g);

	gam_rc_t getGAM(const uint8_t mode, const uint8_t reg, const word_mode_t word_mode, const bool read_value = true);
	gam_rc_t getGAMAddress(const uint8_t mode, const int reg, const word_mode_t word_mode);
	bool     putGAM(const gam_rc_t & g, const uint16_t value); // returns false when flag registers should not be updated

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
	};

	std::optional<operand_parameters> addressing_to_string(const uint8_t mode_register, const uint16_t pc, const word_mode_t word_mode) const;

	void add_to_stack_trace(const uint16_t p);
	void pop_from_stack_trace();

public:
	explicit cpu(bus *const b, std::atomic_uint32_t *const event);
	~cpu();

	JsonDocument serialize();
	static cpu *deserialize(const JsonVariantConst j, bus *const b, std::atomic_uint32_t *const event);

	std::optional<std::string> check_breakpoint();
	int set_breakpoint(breakpoint *const bp);
	bool remove_breakpoint(const int bp_id);
	std::map<int, breakpoint *> list_breakpoints();

	void disassemble(void) const;
	std::map<std::string, std::vector<std::string> > disassemble(const uint16_t addr) const;

	bus *getBus() { return b; }

	void emulation_start();
	uint64_t get_instructions_executed_count() const;
	uint64_t get_wait_time() const { return wait_time; }
	std::tuple<double, double, uint64_t, uint32_t, double> get_mips_rel_speed(const std::optional<uint64_t> & instruction_count, const std::optional<uint64_t> & t_diff_1s) const;
	// how many ms would've really passed when executing `instruction_count` instructions
	uint32_t get_effective_run_time(const uint64_t instruction_count) const;

	bool get_debug() const { return debug_mode; }
	void set_debug(const bool d) { debug_mode = d; stacktrace.clear(); }
	std::vector<std::pair<uint16_t, std::string> > get_stack_trace() const;

	void reset();

	bool step();

	void pushStack(const uint16_t v);
	uint16_t popStack();

	void init_interrupt_queue();
	void queue_interrupt(const uint8_t level, const uint8_t vector);
	std::map<uint8_t, std::set<uint8_t> > get_queued_interrupts() const { return queued_interrupts; }
	std::optional<int> get_interrupt_delay_left() const { return trap_delay; }
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
	void setPSW(const uint16_t v, const bool limited);

	uint16_t getStackLimitRegister() { return stackLimitRegister; }
	void setStackLimitRegister(const uint16_t v) { stackLimitRegister = v; }

	uint16_t get_stackpointer(const int which) const { assert(which >= 0 && which < 4); return sp[which]; }
	uint16_t getPC() const { return pc; }
	void set_stackpointer(const int which, const uint16_t value) { assert(which >= 0 && which < 4); sp[which] = value; }
	void setPC(const uint16_t value) { pc = value; }

	void set_register(const int nr, const uint16_t value);
	void set_registerLowByte(const int nr, const word_mode_t word_mode, const uint16_t value);
	// used by 'main' for json-validation
	void lowlevel_register_set(const uint8_t set, const uint8_t reg, const uint16_t value);
	void lowlevel_register_sp_set(const uint8_t set, const uint16_t value);
	uint16_t lowlevel_register_get(const uint8_t set, const uint8_t reg);
	void lowlevel_psw_set(const uint16_t value) { psw = value; }
	uint16_t lowlevel_register_sp_get(const uint8_t nr) const { return sp[nr]; }

	uint16_t get_register(const int nr) const;

	bool put_result(const gam_rc_t & g, const uint16_t value);
};
