// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <assert.h>
#include <stdint.h>

#include "bus.h"

class cpu
{
private:
	uint16_t regs0_5[2][6]; // R0...5, selected by bit 11 in PSW, 
	uint16_t sp[3+1]; // stackpointers, MF../MT.. select via 12/13 from PSW, others via 14/15
	uint16_t pc { 0 };
	uint16_t psw { 0 }, fpsr { 0 };
	uint16_t stackLimitRegister { 0 };
	bool haltFlag { false }, resetFlag { false };
	bool runMode  { false };

	bool emulateMFPT { false };

	bus *const b { nullptr };

	uint16_t getRegister(const int nr, const bool MF_MT) const;
	void setRegister(const int nr, const bool MF_MT, const uint16_t value);
	void addRegister(const int nr, const bool MF_MT, const uint16_t value);

	uint16_t getGAMAddress(const uint8_t mode, const int reg, const bool word_mode, const bool MF_MT);
	uint16_t getGAM(const uint8_t mode, const uint8_t reg, const bool word_mode, const bool MF_MT);
	void putGAM(const uint8_t mode, const int reg, const bool word_mode, const uint16_t value, const bool MF_FT);

	bool double_operand_instructions(const uint16_t instr);
	bool additional_double_operand_instructions(const uint16_t instr);
	bool single_operand_instructions(const uint16_t instr);
	bool conditional_branch_instructions(const uint16_t instr);
	bool condition_code_operations(const uint16_t instr);
	bool misc_operations(const uint16_t instr);

	std::pair<std::string, int> addressing_to_string(const uint8_t mode_register, const uint16_t pc);
	void disassemble();

public:
	explicit cpu(bus *const b);
	~cpu();

	bus *getBus() { return b; }

	void reset();

	bool step(); // FIXME return mask of flags (halt, reset, etc)
	void resetHalt() { haltFlag = false; }
	void resetReset() { resetFlag = false; }

	void pushStack(const uint16_t v);
	uint16_t popStack();

	void busError();
	void trap(const uint16_t vector);

	void setEmulateMFPT(const bool v) { emulateMFPT = v; }

	bool getRunMode() { return runMode; }

	bool getPSW_c() const;
	bool getPSW_v() const;
	bool getPSW_z() const;
	bool getPSW_n() const;
	bool getBitPSW(const int bit) const;

	void setPSW_c(const bool v);
	void setPSW_v(const bool v);
	void setPSW_z(const bool v);
	void setPSW_n(const bool v);
	void setPSW_spl(const int v);
	void setBitPSW(const int bit, const bool v);

	uint16_t getPSW() const { return psw; }
	void setPSW(const uint16_t v) { psw = v; }

	uint16_t getStackLimitRegister() { return stackLimitRegister; }
	void setStackLimitRegister(const uint16_t v) { stackLimitRegister = v; }

	uint16_t getRegister(const bool user, const int nr) const { return regs0_5[user][nr]; }
	uint16_t getStackPointer(const int which) const { assert(which >= 0 && which < 4); return sp[which]; }
	uint16_t getPC() const { return pc; }

	void setRegister(const bool user, const int nr, const uint16_t value) { regs0_5[user][nr] = value; }
	void setStackPointer(const int which, const uint16_t value) { assert(which >= 0 && which < 4); sp[which] = value; }
	void setPC(const uint16_t value) { pc = value; }

	uint16_t getRegister(const int nr) const { return getRegister(nr, false); } // FIXME remove
	void setRegister(const int nr, const uint16_t v) { setRegister(nr, false, v); } // FIXME remove
};
