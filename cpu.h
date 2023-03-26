// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <assert.h>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdint.h>
#include <vector>

#include "bus.h"

typedef struct {
	word_mode_t word_mode;
	rm_selection_t mode_selection;
	d_i_space_t space;

	std::optional<uint16_t> addr;
	std::optional<int> reg;

	std::optional<uint16_t> value;
} gam_rc_t;

class cpu
{
private:
	uint16_t regs0_5[2][6]; // R0...5, selected by bit 11 in PSW, 
	uint16_t sp[3 + 1]; // stackpointers, MF../MT.. select via 12/13 from PSW, others via 14/15
	uint16_t pc    { 0 };
	uint16_t psw   { 0 };
	uint16_t fpsr  { 0 };
	uint16_t stackLimitRegister { 0377 };
	uint64_t instruction_count { 0 };
	uint64_t running_since     { 0 };

	uint64_t mtpi_count { 0 };

	// level, vector
	std::map<uint8_t, std::set<uint8_t> > queued_interrupts;
	std::mutex              qi_lock;
	std::condition_variable qi_cv;

	std::set<uint16_t> breakpoints;

	bus *const b { nullptr };

	std::atomic_uint32_t *const event { nullptr };

	bool check_queued_interrupts();

	uint16_t addRegister(const int nr, const rm_selection_t mode_selection, const uint16_t value);

	void     addToMMR1(const uint8_t mode, const uint8_t reg, const word_mode_t word_mode);


	gam_rc_t getGAM(const uint8_t mode, const uint8_t reg, const word_mode_t word_mode, const rm_selection_t mode_selection, const bool read_value = true);
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
	};

	operand_parameters addressing_to_string(const uint8_t mode_register, const uint16_t pc, const word_mode_t word_mode) const;

public:
	explicit cpu(bus *const b, std::atomic_uint32_t *const event);
	~cpu();

	bool check_breakpoint();
	void set_breakpoint(const uint16_t addr);
	void remove_breakpoint(const uint16_t addr);
	std::set<uint16_t> list_breakpoints();

	void disassemble(void) const;
	std::map<std::string, std::vector<std::string> > disassemble(const uint16_t addr) const;

	bus *getBus() { return b; }

	void emulation_start();
	uint64_t get_instructions_executed_count();
	std::tuple<double, double, uint64_t> get_mips_rel_speed();

	void reset();

	void step_a();
	void step_b();

	void pushStack(const uint16_t v);
	uint16_t popStack();

	void init_interrupt_queue();
	void queue_interrupt(const uint8_t level, const uint8_t vector);

	void trap(uint16_t vector, const int new_ipl = -1, const bool is_interrupt = false);

	bool getPSW_c() const;
	bool getPSW_v() const;
	bool getPSW_z() const;
	bool getPSW_n() const;
	int  getPSW_spl() const;
	bool getBitPSW(const int bit) const;
	int  getPSW_runmode() const { return psw >> 14; };
	int  getPSW_prev_runmode() const { return (psw >> 12) & 3; };

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

	uint16_t getStackPointer(const int which) const { assert(which >= 0 && which < 4); return sp[which]; }
	uint16_t getPC() const { return pc; }

	void setRegister(const int nr, const uint16_t value, const rm_selection_t mode_selection = rm_cur);

	void setRegisterLowByte(const int nr, const word_mode_t word_mode, const uint16_t value);

	void setStackPointer(const int which, const uint16_t value) { assert(which >= 0 && which < 4); sp[which] = value; }
	void setPC(const uint16_t value) { pc = value; }

	uint16_t getRegister(const int nr, const rm_selection_t mode_selection = rm_cur) const;

	bool put_result(const gam_rc_t & g, const uint16_t value);
};
