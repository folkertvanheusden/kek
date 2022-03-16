// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "utils.h"
#include "gen.h"

#ifndef _DEBUG
std::string *src_gam_text = NULL, *dst_gam_text = NULL;
#endif

#define SIGN(x, wm) ((wm) ? (x) & 0x80 : (x) & 0x8000)

cpu::cpu(bus *const b) : b(b)
{
	reset();
}

cpu::~cpu()
{
}

void cpu::reset()
{
	memset(regs0_5, 0x00, sizeof regs0_5);
	memset(sp, 0x00, sizeof sp);
	pc = 0;
	psw = fpsr = 0;
	runMode = resetFlag = haltFlag = false;
}

uint16_t cpu::getRegister(const int nr, const bool prev_mode) const
{
	if (nr < 6)
		return regs0_5[getBitPSW(11)][nr];

	if (nr == 6) {
		if (prev_mode)
			return sp[(getBitPSW(13) << 1) | getBitPSW(12)];

		return sp[(getBitPSW(15) << 1) | getBitPSW(14)];
	}

	return pc;
}

void cpu::setRegister(const int nr, const bool prev_mode, const uint16_t value)
{
	if (nr < 6)
		regs0_5[getBitPSW(11)][nr] = value;
	else if (nr == 6) {
		if (prev_mode)
			sp[(getBitPSW(13) << 1) | getBitPSW(12)] = value;
		else
			sp[(getBitPSW(15) << 1) | getBitPSW(14)] = value;
	}
	else {
		pc = value;
	}
}

void cpu::addRegister(const int nr, const bool prev_mode, const uint16_t value)
{
	if (nr < 6)
		regs0_5[getBitPSW(11)][nr] += value;
	else if (nr == 6) {
		if (prev_mode)
			sp[(getBitPSW(13) << 1) | getBitPSW(12)] += value;
		else
			sp[(getBitPSW(15) << 1) | getBitPSW(14)] += value;
	}
	else {
		assert((pc & 1) == 0);
		pc += value;
		assert((pc & 1) == 0);
	}
}

bool cpu::getBitPSW(const int bit) const
{
	return !!(psw & (1 << bit));
}

bool cpu::getPSW_c() const
{
	return getBitPSW(0);
}

bool cpu::getPSW_v() const
{
	return getBitPSW(1);
}

bool cpu::getPSW_z() const
{
	return getBitPSW(2);
}

bool cpu::getPSW_n() const
{
	return getBitPSW(3);
}

void cpu::setBitPSW(const int bit, const bool v)
{
	const uint16_t mask = 1 << bit;

	if (v)
		psw |= mask;
	else
		psw &= ~mask;
}

void cpu::setPSW_c(const bool v)
{
	setBitPSW(0, v);
}

void cpu::setPSW_v(const bool v)
{
	setBitPSW(1, v);
}

void cpu::setPSW_z(const bool v)
{
	setBitPSW(2, v);
}

void cpu::setPSW_n(const bool v)
{
	setBitPSW(3, v);
}

void cpu::setPSW_spl(const int v)
{
	psw &= 7 << 5;
	psw |= (v & 7) << 5;
}

// GAM = general addressing modes
uint16_t cpu::getGAM(const uint8_t mode, const uint8_t reg, const bool word_mode, const bool prev_mode, std::string *const text)
{
	uint16_t next_word = 0, temp = 0;

	switch(mode) {
		case 0: // 000 
			D(*text = format("R%d", reg);)
			return getRegister(reg, prev_mode) & (word_mode ? 0xff : 0xffff);
		case 1:
			D(*text = format("(R%d)", reg);)
			return b -> read(getRegister(reg, prev_mode), word_mode, prev_mode);
		case 2:
			temp = b -> read(getRegister(reg, prev_mode), word_mode, prev_mode);
			if (reg == 7 || reg == 6)
				addRegister(reg, prev_mode, 2);
			else
				addRegister(reg, prev_mode, word_mode ? 1 : 2);
#if _DEBUG
			if (reg == 7)
				*text = format("#%o", temp);
			else
				*text = format("(R%d)+", reg);
#endif
			return temp;
		case 3:
			D(*text = format("@(R%d)+", reg);)
			temp = b -> read(b -> read(getRegister(reg, prev_mode), false, prev_mode), word_mode, prev_mode);
			addRegister(reg, prev_mode, 2);
			return temp;
		case 4:
			D(*text = format("-(R%d)", reg);)
			if (reg == 7 || reg == 6)
				addRegister(reg, prev_mode, - 2);
			else
				addRegister(reg, prev_mode, word_mode ? -1 : -2);
			return b -> read(getRegister(reg, prev_mode), word_mode, prev_mode);
		case 5:
			D(*text = format("@-(R%d)", reg);)
			addRegister(reg, prev_mode, -2);
			return b -> read(b -> read(getRegister(reg, prev_mode), false, prev_mode), word_mode, prev_mode);
		case 6:
			next_word = b -> read(getPC(), false, prev_mode);
			//fprintf(stderr, "next word %o\n", next_word);
			addRegister(7, prev_mode, + 2);
			temp = b -> read(getRegister(reg, prev_mode) + next_word, word_mode, prev_mode);
			//fprintf(stderr, "-> %d: %o\n", word_mode, temp);
#if !defined(NDEBUG) && !defined(ESP32)
			if (reg == 7)
				*text = format("0o%o", getPC() + next_word); // FIXME
			else
				*text = format("0o%o(R%d)", next_word, reg);
#endif
			return temp;
		case 7:
			next_word = b -> read(getPC(), false, prev_mode);
			addRegister(7, prev_mode, + 2);
			D(*text = format("@0o%o(R%d)", next_word, reg);)
			return b -> read(b -> read(getRegister(reg, prev_mode) + next_word, false, prev_mode), word_mode, prev_mode);
	}

	return -1;
}

void cpu::putGAM(const uint8_t mode, const int reg, const bool word_mode, const uint16_t value, bool const prev_mode, std::string *const text)
{
	uint16_t next_word = 0;

	switch(mode) {
		case 0:
			D(*text = format("R%d", reg);)
			if (word_mode) {
				uint16_t temp = getRegister(reg, prev_mode);
				temp &= 0xff00;
				temp |= value;
				setRegister(reg, prev_mode, temp);
			}
			else {
				setRegister(reg, prev_mode, value);
			}
			break;
		case 1:
			D(*text = format("(R%d)", reg);)
				b -> write(getRegister(reg, prev_mode), word_mode, value);
			break;
		case 2:
			D(*text = format("(R%d)+", reg);)
			b -> write(getRegister(reg, prev_mode), word_mode, value);
			if (reg == 7 || reg == 6)
				addRegister(reg, prev_mode, 2);
			else
				addRegister(reg, prev_mode, word_mode ? 1 : 2);
			break;
		case 3:
			D(*text = format("@(R%d)+", reg);)
			b -> write(b -> readWord(getRegister(reg, prev_mode)), word_mode, value);
			addRegister(reg, prev_mode, 2);
			break;
		case 4:
			D(*text = format("-(R%d)", reg);)
			if (reg == 7 || reg == 6)
				addRegister(reg, prev_mode, -2);
			else
				addRegister(reg, prev_mode, word_mode ? -1 : -2);
			b -> write(getRegister(reg, prev_mode), word_mode, value);
			break;
		case 5:
			D(*text = format("@-(R%d)", reg);)
			addRegister(reg, prev_mode, -2);
			b -> write(b -> readWord(getRegister(reg, prev_mode)), word_mode, value);
			break;
		case 6:
			next_word = b -> readWord(getPC());
			addRegister(7, prev_mode, 2);
			D(*text = format("0o%o(R%d)", next_word, reg);)
			b -> write(getRegister(reg, prev_mode) + next_word, word_mode, value);
			break;
		case 7:
			next_word = b -> readWord(getPC());
			addRegister(7, prev_mode, 2);
			D(*text = format("@0o%o(R%d)", next_word, reg);)
			b -> write(b -> readWord(getRegister(reg, prev_mode) + next_word), word_mode, value);
			break;

		default:
			// error
			break;
	}
}

uint16_t cpu::getGAMAddress(const uint8_t mode, const int reg, const bool word_mode, const bool prev_mode)
{
	uint16_t next_word = 0, temp = 0;

	switch(mode) {
		case 0:
			// registers are also mapped in memory
			return 0177700 + reg;
		case 1:
			return getRegister(reg, prev_mode);
		case 2:
			temp = getRegister(reg, prev_mode);
			if (reg == 6 || reg == 7)
				addRegister(reg, prev_mode, 2);
			else
				addRegister(reg, prev_mode, word_mode ? 1 : 2);
			return temp;
		case 3:
			temp = b -> readWord(getRegister(reg, prev_mode));
			addRegister(reg, prev_mode, 2);
			return temp;
		case 4:
			if (reg == 6 || reg == 7)
				addRegister(reg, prev_mode, -2);
			else
				addRegister(reg, prev_mode, word_mode ? -1 : -2);
			return getRegister(reg, prev_mode);
		case 5:
			addRegister(reg, prev_mode, -2);
			return b -> readWord(getRegister(reg, prev_mode));
		case 6:
			next_word = b -> readWord(getPC());
			addRegister(7, prev_mode, 2);
			return getRegister(reg, prev_mode) + next_word;
		case 7:
			next_word = b -> readWord(getPC());
			addRegister(7, prev_mode, 2);
			return b -> readWord(getRegister(reg, prev_mode) + next_word);
	}

	return -1;
}

bool cpu::double_operand_instructions(const uint16_t instr)
{
	bool word_mode = !!(instr & 0x8000);

	uint8_t operation = (instr >> 12) & 7;

	if (operation == 0b000)
		return single_operand_instructions(instr);

	if (operation == 0b111)
		return additional_double_operand_instructions(instr);

	const uint8_t src = (instr >> 6) & 63;
	const uint8_t src_mode = (src >> 3) & 7;
	const uint8_t src_reg = src & 7;

#if !defined(NDEBUG) && !defined(ESP32)
	std::string debug_a, debug_b;
	std::string *src_gam_text = &debug_a, *dst_gam_text = &debug_b;
#endif
	uint16_t src_value;

	const uint8_t dst = instr & 63;
	const uint8_t dst_mode = (dst >> 3) & 7;
	const uint8_t dst_reg = dst & 7;

	switch(operation) {
		case 0b001: // MOV/MOVB Move Word/Byte
			src_value = getGAM(src_mode, src_reg, word_mode, false, src_gam_text);
			if (word_mode) {
				if (dst_mode == 0)
					setRegister(dst_reg, false, int8_t(src_value));
				else
					putGAM(dst_mode, dst_reg, word_mode, src_value, false, dst_gam_text);
			}
			else {
				putGAM(dst_mode, dst_reg, word_mode, src_value, false, dst_gam_text);
			}
			D(fprintf(stderr, "MOV%c %s to %s\n", word_mode ? 'B' : ' ', src_gam_text -> c_str(), dst_gam_text -> c_str());)

			setPSW_n(SIGN(src_value, word_mode));
			setPSW_z(src_value == 0);
			setPSW_v(false);

			return true;

		case 0b010: { // CMP/CMPB Compare Word/Byte
				    //fprintf(stderr, "src mode %d src reg %d, dst mode %d dst reg %d\n", src_mode, src_reg, dst_mode, dst_reg);
				    src_value = getGAM(src_mode, src_reg, word_mode, false, src_gam_text);
				    uint16_t dst_value = getGAM(dst_mode, dst_reg, word_mode, false, dst_gam_text);

				    uint16_t temp = (src_value - dst_value) & (word_mode ? 0xff : 0xffff);
				    setPSW_n(SIGN(temp, word_mode));
				    setPSW_z(temp == 0);

				    setPSW_v(SIGN((src_value ^ dst_value) & (~dst_value ^ temp), word_mode));
				    // fprintf(stderr, "SIGNsimh: %d\n", SIGN((src_value ^ dst_value) & (~dst_value ^ temp), word_mode));
				    // fprintf(stderr, "SIGNme__: %d\n", SIGN(src_value, word_mode) != SIGN(dst_value, word_mode) && SIGN(dst_value, word_mode) == SIGN(temp, word_mode));

				    setPSW_c(src_value < dst_value);

				    //fprintf(stderr, "%o - %o > %o | %d, %d\n", 
				    //	    src_value, dst_value, src_value - dst_value,
				    //	    SIGN((src_value ^ dst_value) & (~dst_value ^ temp), word_mode),
				    //	    sign(src_value) != sign(dst_value) && sign(src_value) == sign(temp));

				    D(fprintf(stderr, "CMP%c %s to %s    n%dz%dv%dc%d\n", word_mode ? 'B' : ' ', src_gam_text -> c_str(), dst_gam_text -> c_str(), getPSW_n(), getPSW_z(), getPSW_v(), getPSW_c());)
				    //	fprintf(stderr, "%o %o %o\n", b -> readWord(017026), getPC(), b -> readWord(017026 + getPC()));
				    return true;
			    }

		case 0b011: { // BIT/BITB Bit Test Word/Byte
				    src_value = getGAM(src_mode, src_reg, word_mode, false, src_gam_text);
				    uint16_t dst_value = getGAM(dst_mode, dst_reg, word_mode, false, dst_gam_text);
				    uint16_t result = dst_value & src_value;
				    D(fprintf(stderr, "BIT%c %s to %s\n", word_mode ? 'B' : ' ', src_gam_text -> c_str(), dst_gam_text -> c_str());)
				    setPSW_n(SIGN(result, word_mode));
				    setPSW_z(result == 0);
				    setPSW_v(false);
				    return true;
			    }

		case 0b100: { // BIC/BICB Bit Clear Word/Byte
				    src_value = getGAM(src_mode, src_reg, word_mode, false, src_gam_text);
				    uint16_t a = getGAMAddress(dst_mode, dst_reg, word_mode, false);

				    uint16_t result = b -> readWord(a) & ~src_value;

				    if (dst_mode == 0) {
					    putGAM(dst_mode, dst_reg, word_mode, result, false, dst_gam_text);
					    D(fprintf(stderr, "BIC%c %s to R%d\n", word_mode ? 'B' : ' ', src_gam_text -> c_str(), dst_reg);)
				    }
				    else {
					    b -> write(a, word_mode, result);
					    D(fprintf(stderr, "BIC%c %s to @%o\n", word_mode ? 'B' : ' ', src_gam_text -> c_str(), a);)
				    }

				    setPSW_n(SIGN(result, word_mode));
				    setPSW_z(result == 0);
				    setPSW_v(false);
				    return true;
			    }

		case 0b101: { // BIS/BISB Bit Set Word/Byte
				    src_value = getGAM(src_mode, src_reg, word_mode, false, src_gam_text);
				    uint16_t a = getGAMAddress(dst_mode, dst_reg, word_mode, false);

				    uint16_t result = b -> readWord(a) | src_value;

				    if (dst_mode == 0)
					    putGAM(dst_mode, dst_reg, word_mode, result, false, dst_gam_text);
				    else {
#if !defined(NDEBUG) && !defined(ESP32)
					    dst_gam_text -> assign(format("(%o)", a));
#endif
					    b -> write(a, word_mode, result);
				    }

				    setPSW_n(SIGN(result, word_mode));
				    setPSW_z(result == 0);
				    setPSW_v(false);
				    D(fprintf(stderr, "BIS%c %s to %s\n", word_mode ? 'B' : ' ', src_gam_text -> c_str(), dst_gam_text -> c_str());)
				    return true;
			    }

		case 0b110: { // ADD/SUB Add/Subtract Word
				    int16_t src_value = getGAM(src_mode, src_reg, false, false, src_gam_text);
				    uint16_t da = getGAMAddress(dst_mode, dst_reg, false, false);
				    int16_t dst_value = b -> readWord(da);
				    int16_t result = 0;
				    if (instr & 0x8000) {
					    result = (dst_value + ~src_value + 1) & 0xffff;
					    setPSW_v(sign(src_value) != sign(dst_value) && sign(src_value) == sign(result));
					    setPSW_c(uint16_t(dst_value) < uint16_t(src_value));
				    }
				    else {
					    result = dst_value + src_value;
					    setPSW_v(sign(src_value) == sign(dst_value) && sign(dst_value) != sign(result));
					    setPSW_c(uint16_t(result) < uint16_t(src_value));
				    }
				    setPSW_n(result < 0);
				    setPSW_z(result == 0);
				    if (dst_mode == 0)
					    setRegister(dst_reg, false, result);
				    else
					    b -> writeWord(da, result);
				    D(fprintf(stderr, "%s %s to %s      (%o (%d), %o (%d)) = %d\n", (instr & 0x8000) ? "SUB" : "ADD", src_gam_text -> c_str(), dst_gam_text -> c_str(), src_value, src_value, dst_value, dst_value, result);)
				    return true;
			    }
	}

	return false;
}

bool cpu::additional_double_operand_instructions(const uint16_t instr)
{
	const uint8_t reg = (instr >> 6) & 7;

#if !defined(NDEBUG) && !defined(ESP32)
	std::string debug_b;
	std::string *dst_gam_text = &debug_b;
#endif

	const uint8_t dst = instr & 63;
	const uint8_t dst_mode = (dst >> 3) & 7;
	const uint8_t dst_reg = dst & 7;

	int operation = (instr >> 9) & 7;

	switch(operation) {
		case 0: { // MUL
				D(fprintf(stderr, "MUL\n");)
				uint16_t R = getRegister(reg);
				int32_t result = R * getGAM(dst_mode, dst_reg, true, false, dst_gam_text);

				if (reg & 1)
					setRegister(reg, result >> 16);
				else {
					setRegister(reg, result & 65535);
					setRegister(reg + 1, result >> 16);
				}

				setPSW_n(result < 0);
				setPSW_z(result == 0);
				setPSW_z(result < -32768 || result > 32767);
				return true;
			}

		case 1: { // DIV
				int32_t R0R1 = (getRegister(reg) << 16) | getRegister(reg + 1);
				int32_t divider = getGAM(dst_mode, dst_reg, true, false, dst_gam_text);

				if (divider == 0) {
					setPSW_n(false);
					setPSW_z(true);
					setPSW_v(true);
					setPSW_c(true);
				}
				else {
					int32_t quot = R0R1 / divider;
					uint16_t rem = R0R1 % divider;

					D(fprintf(stderr, "DIV R%d,%s \t%d/%d = %d,%d\n", reg, dst_gam_text -> c_str(), R0R1, divider, quot, rem);)

						setRegister(reg, quot);
					setRegister(reg + 1, rem);

					setPSW_n(R0R1 / divider < 0);
					setPSW_z(quot == 0);
					setPSW_v(quot > 0xffff || quot < -0xffff);
					setPSW_c(false);
				}

				return true;
			}

		case 2: { // ASH
				int16_t R = getRegister(reg), oldR = R;
				int8_t shift = getGAM(dst_mode, dst_reg, true, false, dst_gam_text);
				D(fprintf(stderr, "ASH R%d,%d\n", reg, shift);)

					if (shift > 0) {
						R <<= shift - 1;
						setPSW_c(R & 0x8000);
						R <<= 1;
					}
					else if (shift < 0) {
						R >>= -shift - 1;
						setPSW_c(R & 1);
						R >>= 1;
					}

				setPSW_n(R < 0);
				setPSW_z(R == 0);
				setPSW_v(sign(R) != sign(oldR));

				setRegister(reg, R);
				return true;
			}

		case 3: { // ASHC
				D(fprintf(stderr, "ASHC\n");)
					uint32_t R0R1 = (getRegister(reg) << 16) | getRegister(reg + 1);
				int16_t shift = getGAM(dst_mode, dst_reg, true, false, dst_gam_text);

				if (shift > 0) {
					R0R1 <<= (shift & 0b111111) - 1;
					setPSW_c(R0R1 >> 31);
					R0R1 <<= 1;
				}
				else if (shift < 0) {
					R0R1 >>= -(shift & 0b111111) - 1;
					setPSW_c(R0R1 & 1);
					R0R1 >>= 1;
				}

				setRegister(reg, R0R1 & 65535);
				setRegister(reg + 1, R0R1 >> 16);

				setPSW_n(R0R1 >> 31);
				setPSW_z(R0R1 == 0);

				return true;
			}

		case 4: { // XOR (word only)
				D(fprintf(stderr, "XOR\n");)
				uint16_t src_value = getGAM(dst_mode, dst_reg, true, false, dst_gam_text) ^ getRegister(reg);
				putGAM(dst_mode, dst_reg, false, src_value, false, dst_gam_text);
				setPSW_n(src_value & 0x8000);
				setPSW_z(src_value == 0);
				setPSW_v(false);
				return true;
			}

		case 7: { // SOB
			D(fprintf(stderr, "SOB    (%o)\n", reg);)
			uint16_t oldPC = getPC(); // FIXME gaat dit wel goed voor R7?
			addRegister(reg, false, -1);
			if (getRegister(reg, false)) {
				uint16_t newPC = oldPC - dst * 2;
				D(fprintf(stderr, " jump back from %o to %o\n", oldPC, newPC);)
				setPC(newPC);
			}
			return true;
			}
	}

	return false;
}

bool cpu::single_operand_instructions(const uint16_t instr)
{
	const uint16_t opcode = (instr >> 6) & 0b111111111;
	const uint8_t dst = instr & 63;
	const uint8_t dst_mode = (dst >> 3) & 7;
	const uint8_t dst_reg = dst & 7;
	const bool word_mode = !!(instr & 0x8000);
	uint16_t a = -1;
	int32_t vl = -1;
	uint16_t v = -1;

#if !defined(NDEBUG) && !defined(ESP32)
	std::string debug_b;
	std::string *dst_gam_text = &debug_b;
	std::string debug_b2;
	std::string *src_gam_text = &debug_b2;
#endif

	switch(opcode) {
		case 0b00000011: // SWAB
			if (word_mode) // handled elsewhere
				return false;
			else {
				D(fprintf(stderr, "SWAB ");)
				a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
				uint8_t t1, t2;
				uint16_t t;
				if (dst_mode == 0) {
					D(fprintf(stderr, "R%d\n", dst_reg);)
					t = getRegister(dst_reg, false);
					t1 = t >> 8;
					t2 = t & 255;
					setRegister(dst_reg, false, (t2 << 8) | t1);
				}
				else {
					D(fprintf(stderr, "\n");)
					t = getRegister(dst_reg, false);
					t1 = b -> readByte(a);
					t2 = b -> readByte(a + 1);

					b -> writeByte(a, t2);
					b -> writeByte(a + 1, t1);
				}

				setPSW_n(t2 & 0x80);
				setPSW_z(t2 == 0);
				setPSW_v(false);
				setPSW_c(false);
				break;
			}

		case 0b000101000: // CLR/CLRB
			a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
			D(fprintf(stderr, "CLR (wm%d, mode%d,reg%d): addr %o, value %o\n", word_mode, dst_mode, dst_reg, a, b -> read(a, word_mode));)
			if (dst_mode == 0)
				putGAM(dst_mode, dst_reg, word_mode, 0, false, dst_gam_text);
			else
				b -> write(a, word_mode, 0);
			setPSW_n(false);
			setPSW_z(true);
			setPSW_v(false);
			setPSW_c(false);
			break;

		case 0b000101001: // COM/COMB
			D(fprintf(stderr, "COM%c\n", word_mode ? 'B' : ' ');)
			a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
			vl = b -> read(a, word_mode);
			if (word_mode)
				vl ^= 0xff;
			else
				vl ^= 0xffff;

			setPSW_n(SIGN(vl, word_mode));
			setPSW_z(vl == 0);
			setPSW_v(false);
			setPSW_c(true);

			if (dst_mode == 0)
				putGAM(dst_mode, dst_reg, word_mode, vl, false, dst_gam_text);
			else
				b -> write(a, word_mode, vl);

			break;

		case 0b000101010: // INC/INCB
			D(fprintf(stderr, "INC%c dst-mode %d\n", word_mode ? 'B' : ' ', dst_mode);)
			a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
			D(fprintf(stderr, " read from %o\n", a);)
			v = b -> read(a, word_mode);
			vl = (v + 1) & (word_mode ? 0xff : 0xffff);
			setPSW_n(word_mode ? vl > 127 : vl > 32767);
			setPSW_z(vl == 0);
			setPSW_v(word_mode ? v == 0x7f : v == 0x7fff);
			if (dst_mode == 0)
				putGAM(dst_mode, dst_reg, word_mode, vl, false, dst_gam_text);
			else
				b -> write(a, word_mode, vl);
			break;

		case 0b000101011: // DEC/DECB
			D(fprintf(stderr, "DEC%c\n", word_mode ? 'B' : ' ');)
				a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
			v = b -> read(a, word_mode);
			vl = (v - 1) & (word_mode ? 0xff : 0xffff);
			setPSW_n(word_mode ? vl > 127 : vl > 32767);
			setPSW_z(vl == 0);
			setPSW_v(word_mode ? v == 0x80 : v == 0x8000);
			if (dst_mode == 0)
				putGAM(dst_mode, dst_reg, word_mode, vl, false, dst_gam_text);
			else
				b -> write(a, word_mode, vl);
			break;

		case 0b000101100: // NEG/NEGB
			D(fprintf(stderr, "NEG%c\n", word_mode ? 'B' : ' ');)
				a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
			v = b -> read(a, word_mode);
			vl = word_mode ? uint8_t(-int8_t(v)) : -int16_t(v);
			if (dst_mode == 0)
				putGAM(dst_mode, dst_reg, word_mode, vl, false, dst_gam_text);
			else
				b -> write(a, word_mode, vl);
			setPSW_n(SIGN(vl, word_mode));
			setPSW_z(vl == 0);
			setPSW_v(word_mode ? vl == 0x80 : vl == 0x8000);
			setPSW_c(vl);
			break;

		case 0b000101101: { // ADC/ADCB
					  D(fprintf(stderr, "ADC%c\n", word_mode ? 'B' : ' ');)
					  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
					  uint16_t org = b -> read(a, word_mode);
					  uint16_t new_ = org + getPSW_c();
					  if (dst_mode == 0)
						  putGAM(dst_mode, dst_reg, word_mode, new_, false, dst_gam_text);
					  else
						  b -> write(a, word_mode, new_);
					  setPSW_n(SIGN(new_, word_mode));
					  setPSW_z(new_ == 0);
					  setPSW_v((word_mode ? org == 0x7f : org == 0x7fff) && getPSW_c());
					  setPSW_c((word_mode ? org == 0xff : org == 0xffff) && getPSW_c());
					  break;
				  }

		case 0b000101110: // SBC/SBCB
				  D(fprintf(stderr, "SBC%c\n", word_mode ? 'B' : ' ');)
				  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
				  //fprintf(stderr, "%d,%d\n", dst_mode, dst_reg);
				  v = b -> read(a, word_mode);
				  vl = (v - getPSW_c()) & (word_mode ? 0xff : 0xffff);
				  if (dst_mode == 0)
					  putGAM(dst_mode, dst_reg, word_mode, vl, false, dst_gam_text);
				  else
					  b -> write(a, word_mode, vl);
				  setPSW_n(SIGN(vl, word_mode));
				  setPSW_z(vl == 0);
				  setPSW_v(vl == 0x8000);

				  if (v == 0 && getPSW_c())
					  setPSW_c(true);
				  else
					  setPSW_c(false);
				  break;

		case 0b000101111: // TST/TSTB
				  D(fprintf(stderr, "TST%c\n", word_mode ? 'B' : ' ');)
				  v = getGAM(dst_mode, dst_reg, word_mode, false, dst_gam_text);
				  setPSW_n(word_mode ? v & 128 : v & 32768);
				  setPSW_z(v == 0);
				  setPSW_v(false);
				  setPSW_c(false);
				  break;

		case 0b000110000: { // ROR/RORB
					  D(fprintf(stderr, "ROR%c\n", word_mode ? 'B' : ' ');)
					  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
					  uint16_t t = b -> read(a, word_mode);
					  bool new_carry = t & 1;

					  uint16_t temp = 0;
					  if (word_mode) {
						  temp = (t >> 1) | (getPSW_c() << 7);
						  if (dst_mode == 0)
							  putGAM(dst_mode, dst_reg, word_mode, temp, false, dst_gam_text);
						  else
							  b -> writeByte(a, temp);
					  }
					  else {
						  temp = (t >> 1) | (getPSW_c() << 15);
						  if (dst_mode == 0)
							  putGAM(dst_mode, dst_reg, word_mode, temp, false, dst_gam_text);
						  else
							  b -> writeWord(a, temp);
					  }

					  setPSW_c(new_carry);

					  //fprintf(stderr, "%04x\n", temp);
					  setPSW_n(SIGN(temp, word_mode));
					  setPSW_z(temp == 0);
					  setPSW_v(getPSW_c() ^ getPSW_n());

					  break;
				  }

		case 0b000110001: { // ROL/ROLB
					  D(fprintf(stderr, "ROL%c, carry is %d\n", word_mode ? 'B' : ' ', getPSW_c());)
					  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
					  uint16_t t = b -> read(a, word_mode);
					  bool new_carry = false;

					  uint16_t temp;
					  if (word_mode) {
						  new_carry = t & 0x80;
						  temp = ((t << 1) | getPSW_c()) & 0xff;
						  if (dst_mode == 0)
							  putGAM(dst_mode, dst_reg, word_mode, temp, false, dst_gam_text);
						  else
							  b -> writeByte(a, temp);
					  }
					  else {
						  new_carry = t & 0x8000;
						  temp = (t << 1) | getPSW_c();
						  if (dst_mode == 0)
							  putGAM(dst_mode, dst_reg, word_mode, temp, false, dst_gam_text);
						  else
							  b -> writeWord(a, temp);
					  }

					  setPSW_c(new_carry);

					  setPSW_n(SIGN(temp, word_mode));
					  setPSW_z(temp == 0);
					  setPSW_v(getPSW_c() ^ getPSW_n());

					  break;
				  }

		case 0b000110010: { // ASR/ASRB
					  D(fprintf(stderr, "ASR%c\n", word_mode ? 'B' : ' ');)
					  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
					  vl = b -> read(a, word_mode);

					  bool hb = word_mode ? vl & 128 : vl & 32768;

					  setPSW_c(vl & 1);
					  vl >>= 1;
					  if (word_mode)
						  vl |= hb << 7;
					  else
						  vl |= hb << 15;

					  if (dst_mode == 0)
						  putGAM(dst_mode, dst_reg, word_mode, vl, false, dst_gam_text);
					  else
						  b -> write(a, word_mode, vl);

					  setPSW_n(SIGN(vl, word_mode));
					  setPSW_z(vl == 0);
					  setPSW_v(getPSW_n() ^ getPSW_c());
					  break;
				  }

		case 0b00110011: // ASL/ASLB
				  D(fprintf(stderr, "ASL%c\n", word_mode ? 'B' : ' ');)
				  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
				  vl = b -> read(a, word_mode);
				  v = (vl << 1) & (word_mode ? 0xff : 0xffff);
				  setPSW_n(word_mode ? v & 0x80 : v & 0x8000);
				  setPSW_z(v == 0);
				  setPSW_c(word_mode ? vl & 0x80 : vl & 0x8000);
				  setPSW_v(getPSW_n() ^ getPSW_c());
				  if (dst_mode == 0)
					  putGAM(dst_mode, dst_reg, word_mode, v, false, dst_gam_text);
				  else
					  b -> write(a, word_mode, v);
				  break;

		case 0b00110101: // MFPD/MFPI
				  // FIXME
				  v = getGAM(dst_mode, dst_reg, word_mode, true, dst_gam_text);
				  D(fprintf(stderr, "MFPD/MFPI %s\n", dst_gam_text -> c_str());)
				  setPSW_n(word_mode ? v & 0x80 : v & 0x8000);
				  setPSW_z(v == 0);
				  setPSW_v(false);
				  pushStack(v);
				  break;

		case 0b00110110: // MTPI/MTPD
				  // FIXME
				  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
				  D(fprintf(stderr, "MTPI/MTPD\n");)
				  v = popStack();
				  setPSW_n(word_mode ? v & 0x80 : v & 0x8000);
				  setPSW_z(v == 0);
				  setPSW_v(false);
				  if (dst_mode == 0)
					  putGAM(dst_mode, dst_reg, word_mode, v, true, dst_gam_text);
				  else
					  b -> write(a, word_mode, v); // ?
				  break;

		case 0b000110100: // MTPS (put something in PSW)
				  D(fprintf(stderr, "MTPS\n");)
				  psw = getGAM(dst_mode, dst_reg, word_mode, false, src_gam_text);
				  break;

		case 0b000110111: // MFPS (get PSW to something)
				  D(fprintf(stderr, "MFPS\n");)
				  putGAM(dst_mode, dst_reg, word_mode, psw, false, dst_gam_text);
				  break;

		default:
				  return false;
	}

	return true;
}

bool cpu::conditional_branch_instructions(const uint16_t instr)
{
	const uint8_t opcode = (instr >> 8) & 255;
	const int8_t offset = instr & 255;
	bool take = false;
#if !defined(NDEBUG) && !defined(ESP32)
	std::string name;
#endif

	switch(opcode) {
		case 0b00000001: // BR
			take = true;
			D(name = "BR";)
				break;

		case 0b00000010: // BNE
			take = !getPSW_z();
			D(name = "BNE";)
				break;

		case 0b00000011: // BEQ
			take = getPSW_z();
			D(name = "BEQ";)
				break;

		case 0b00000100: // BGE
			take = (getPSW_n() ^ getPSW_v()) == false;
			D(name = "BGE";)
				break;

		case 0b00000101: // BLT
			take = getPSW_n() ^ getPSW_v();
			D(name = "BLT";)
				break;

		case 0b00000110: // BGT
			take = ((getPSW_n() ^ getPSW_v()) | getPSW_z()) == false;
			D(name = "BGT";)
				break;

		case 0b00000111: // BLE
			take = (getPSW_n() ^ getPSW_v()) | getPSW_z();
			D(name = "BLE";)
				break;

		case 0b10000000: // BPL
			take = getPSW_n() == false;
			D(name = "BPL";)
				break;

		case 0b10000001: // BMI
			take = getPSW_n() == true;
			D(name = "BMI";)
				break;

		case 0b10000010: // BHI
			take = getPSW_c() == false && getPSW_z() == false;
			D(name = "BHI";)
				break;

		case 0b10000011: // BLOS
			take = getPSW_c() | getPSW_z();
			D(name = "BLOS";)
				break;

		case 0b10000100: // BVC
			take = getPSW_v() == false;
			D(name = "BVC";)
				break;

		case 0b10000101: // BVS
			take = getPSW_v();
			D(name = "BVS";)
				break;

		case 0b10000110: // BCC
			take = getPSW_c() == false;
			D(name = "BCC";)
			break;

		case 0b10000111: // BCS / BLO
			take = getPSW_c();
			D(name = "BCS/BLO";)
			break;

		default:
			return false;
	}

	D(fprintf(stderr, "%s %o (%d)\n", name.c_str(), offset, take);)
	if (take) {
		D(fprintf(stderr, "branch %d from 0o%o to 0o%o\n", offset * 2, getPC(), getPC() + offset * 2);)
		addRegister(7, false, offset * 2);
	}

	return true;
}

bool cpu::condition_code_operations(const uint16_t instr)
{
	switch(instr) {
		case 0b0000000010100000: // NOP
		case 0b0000000010110000: // NOP
			D(fprintf(stderr, "NOP\n");)
			return true;
	}

	if ((instr & ~7) == 0000230) { // SPLx
		int level = instr & 7;
		D(fprintf(stderr, "SPL %d\n", level);)
		setPSW_spl(level);
		return true;
	}

	if ((instr & ~31) == 0b10100000) { // set condition bits
		D(fprintf(stderr, "%s n%dz%dv%dc%d\n", instr & 0b10000 ? "SET" : "CLR", !!(instr & 8), !!(instr & 4), !!(instr & 2), instr & 1);)
		if (instr & 0b10000) {
			setPSW_n(instr & 0b1000);
			setPSW_z(instr & 0b0100);
			setPSW_v(instr & 0b0010);
			setPSW_c(instr & 0b0001);
		}
		else {
			if (instr & 0b1000)
				setPSW_n(false);
			if (instr & 0b0100)
				setPSW_z(false);
			if (instr & 0b0010)
				setPSW_v(false);
			if (instr & 0b0001)
				setPSW_c(false);
		}

		return true;
	}

	return false;
}

void cpu::pushStack(const uint16_t v)
{
	if (getRegister(6) == stackLimitRegister) {
		printf("stackLimitRegister reached\n");
		exit(1);
	}

	addRegister(6, false, -2);
	b -> writeWord(getRegister(6, false), v);
}

uint16_t cpu::popStack()
{
	uint16_t temp = b -> readWord(getRegister(6, false));
	addRegister(6, false, 2);
	return temp;
}

void cpu::switchModeToKernel()
{
	int previous_mode = (psw >> 14) & 3;
	psw &= 0007777;
	psw |= previous_mode << 12;
}

bool cpu::misc_operations(const uint16_t instr)
{
	switch(instr) {
		case 0b0000000000000000: // HALT
			D(fprintf(stderr, "HALT\n");)
			// pretend HALT is not executed, proceed
			haltFlag = true;
			return true;

		case 0b0000000000000001: // WAIT
			D(fprintf(stderr, "WAIT\n");)
			return true;

		case 0b0000000000000010: // RTI
			D(fprintf(stderr, "RTI\n");)
			setPC(popStack());
			setPSW(popStack());
			return true;

		case 0b0000000000000110: // RTT
			D(fprintf(stderr, "RTT\n");)
			setPC(popStack());
			setPSW(popStack());
			return true;

		case 0b0000000000000111: // MFPT
			D(fprintf(stderr, "MFPT\n");)
			if (emulateMFPT)
				setRegister(0, true, 1); // PDP-11/44
			else {
				pushStack(getPSW());
				pushStack(getPC());
				setPC(b -> readWord(012));
				setPSW(b -> readWord(014));
			}
			return true;

		case 0b0000000000000101: // RESET
			D(fprintf(stderr, "RESET\n");)
			resetFlag = true;
			return true;
	}

	if ((instr >> 8) == 0b10001000) { // EMT
		D(fprintf(stderr, "EMT\n");)
		pushStack(getPSW());
		pushStack(getPC());
		setPC(b -> readWord(030));
		setPSW(b -> readWord(032));
		return true;
	}

	if ((instr >> 8) == 0b10001001) { // TRAP
		pushStack(getPSW());
		pushStack(getPC());
		switchModeToKernel();
		setPC(b -> readWord(034));
		setPSW(b -> readWord(036));
		D(fprintf(stderr, "TRAP (sp: %o, new pc: %o)\n", getRegister(6, false), getPC());)
		return true;
	}

	if ((instr & ~0b111111) == 0b0000000001000000) { // JMP
		int dst_mode = (instr >> 3) & 7, dst_reg = instr & 7;
		bool word_mode = false;
		setPC(getGAMAddress(dst_mode, dst_reg, word_mode, false));
		D(fprintf(stderr, "JMP %o\n", getPC());)
		return true;
	}

	if ((instr & 0b1111111000000000) == 0b0000100000000000) { // JSR
		const int link_reg = (instr >> 6) & 7;
		uint16_t dst_value = getGAMAddress((instr >> 3) & 7, instr & 7, false, false);
		D(fprintf(stderr, "JSR R%d,%o\n", link_reg, dst_value);)

		// PUSH link
		pushStack(getRegister(link_reg, false));

		// MOVE PC,link
		setRegister(link_reg, false, getPC());

		// JMP dst
		setPC(dst_value);

		return true;
	}

	if ((instr & 0b1111111111111000) == 0b0000000010000000) { // RTS
		const int link_reg = instr & 7;

		// MOVE link, PC
		setPC(getRegister(link_reg, false));
		uint16_t temp = getPC();

		// POP link
		setRegister(link_reg, false, popStack());

		D(fprintf(stderr, "RTS new PC: %o, link reg %d: %o\n", temp, link_reg, getRegister(link_reg, false));)

		return true;
	}

	return false;
}

void cpu::busError()
{
	fprintf(stderr, " *** BUS ERROR ***\n");
	//  PSW = 177776
	//  mov @#PSW, -(sp)
	pushStack(getPSW());

	//  mov pc, -(sp)
	pushStack(getPC());

	//  mov @#VEC+2, @#PSW
	setPSW(b -> readWord(6));

	//  mov @#VEC, pc
	setPC(b -> readWord(4));
	fprintf(stderr, " GO TO %o, PSW %o\n", getPC(), getPSW());
}

bool cpu::step()
{
	if (getPC() == 0xbfba) {
		FILE *fh = fopen("debug.dat", "wb");
		for(int i=0; i<256; i++)
			fputc(b -> readByte(getPC() + i), fh);
		fclose(fh);
	}

	if (getPC() & 1) {
		D(fprintf(stderr, "odd addressing\n");)
		busError();
	}

	D(fprintf(stderr, "0x%04x/%d/0o%06o:", getPC(), getPC(), getPC());)
	b -> setMMR2(getPC());
	uint16_t instr = b -> readWord(getPC());
	D(fprintf(stderr, " INSTR 0x%04x 0o%06o\n", instr, instr);)
	addRegister(7, false, 2);
	D(fprintf(stderr, " PC is now %o\n", getPC());)

	//	if (single_operand_instructions(instr))
	//		goto ok;

	if (double_operand_instructions(instr))
		goto ok;

	if (conditional_branch_instructions(instr))
		goto ok;

	if (condition_code_operations(instr))
		goto ok;

	if (misc_operations(instr))
		goto ok;

	fprintf(stderr, "UNHANDLED instruction %o\n\n", instr);

	{
		FILE *fh = fopen("fail.dat", "wb");
		for(int i=0; i<256; i++)
			fputc(b -> readByte(getPC() - 2 + i), fh);
		fclose(fh);
	}
	busError();
	exit(1);
	return false;

	return true;

ok:
#if !defined(NDEBUG) && !defined(ESP32)
	for(int r=0; r<8; r++)
		fprintf(stderr, "%06o ", getRegister(r, false));
	fprintf(stderr, " | n%dz%dv%dc%d P%dC%d S%d", getPSW_n(), getPSW_z(), getPSW_v(), getPSW_c(), (getPSW() >> 12) & 3, getPSW() >> 14, getBitPSW(11));
	fprintf(stderr, "\n\n");
#endif

	return haltFlag; // return flags that indicate that special attention is required
}