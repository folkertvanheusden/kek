// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "gen.h"
#include "utils.h"

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
	psw = 7 << 5;
	fpsr = 0;
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
	psw |= v << 5;
}

// GAM = general addressing modes
uint16_t cpu::getGAM(const uint8_t mode, const uint8_t reg, const bool word_mode, const bool prev_mode)
{
	uint16_t next_word = 0, temp = 0;

	switch(mode) {
		case 0: // 000 
			return getRegister(reg, prev_mode) & (word_mode ? 0xff : 0xffff);
		case 1:
			return b -> read(getRegister(reg, prev_mode), word_mode, prev_mode);
		case 2:
			temp = b -> read(getRegister(reg, prev_mode), word_mode, prev_mode);
			if (reg == 7 || reg == 6)
				addRegister(reg, prev_mode, 2);
			else
				addRegister(reg, prev_mode, word_mode ? 1 : 2);
			return temp;
		case 3:
			temp = b -> read(b -> read(getRegister(reg, prev_mode), false, prev_mode), word_mode, prev_mode);
			addRegister(reg, prev_mode, 2);
			return temp;
		case 4:
			if (reg == 7 || reg == 6)
				addRegister(reg, prev_mode, - 2);
			else
				addRegister(reg, prev_mode, word_mode ? -1 : -2);
			return b -> read(getRegister(reg, prev_mode), word_mode, prev_mode);
		case 5:
			addRegister(reg, prev_mode, -2);
			return b -> read(b -> read(getRegister(reg, prev_mode), false, prev_mode), word_mode, prev_mode);
		case 6:
			next_word = b -> read(getPC(), false, prev_mode);
			addRegister(7, prev_mode, + 2);
			temp = b -> read(getRegister(reg, prev_mode) + next_word, word_mode, prev_mode);
			return temp;
		case 7:
			next_word = b -> read(getPC(), false, prev_mode);
			addRegister(7, prev_mode, + 2);
			return b -> read(b -> read(getRegister(reg, prev_mode) + next_word, false, prev_mode), word_mode, prev_mode);
	}

	return -1;
}

void cpu::putGAM(const uint8_t mode, const int reg, const bool word_mode, const uint16_t value, bool const prev_mode)
{
	uint16_t next_word = 0;

	switch(mode) {
		case 0:
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
			b -> write(getRegister(reg, prev_mode), word_mode, value);
			break;
		case 2:
			b -> write(getRegister(reg, prev_mode), word_mode, value);
			if (reg == 7 || reg == 6)
				addRegister(reg, prev_mode, 2);
			else
				addRegister(reg, prev_mode, word_mode ? 1 : 2);
			break;
		case 3:
			b -> write(b -> readWord(getRegister(reg, prev_mode)), word_mode, value);
			addRegister(reg, prev_mode, 2);
			break;
		case 4:
			if (reg == 7 || reg == 6)
				addRegister(reg, prev_mode, -2);
			else
				addRegister(reg, prev_mode, word_mode ? -1 : -2);
			b -> write(getRegister(reg, prev_mode), word_mode, value);
			break;
		case 5:
			addRegister(reg, prev_mode, -2);
			b -> write(b -> readWord(getRegister(reg, prev_mode)), word_mode, value);
			break;
		case 6:
			next_word = b -> readWord(getPC());
			addRegister(7, prev_mode, 2);
			b -> write(getRegister(reg, prev_mode) + next_word, word_mode, value);
			break;
		case 7:
			next_word = b -> readWord(getPC());
			addRegister(7, prev_mode, 2);
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

	uint16_t src_value;

	const uint8_t dst = instr & 63;
	const uint8_t dst_mode = (dst >> 3) & 7;
	const uint8_t dst_reg = dst & 7;

	switch(operation) {
		case 0b001: // MOV/MOVB Move Word/Byte
			src_value = getGAM(src_mode, src_reg, word_mode, false);
			if (word_mode) {
				if (dst_mode == 0)
					setRegister(dst_reg, false, int8_t(src_value));
				else
					putGAM(dst_mode, dst_reg, word_mode, src_value, false);
			}
			else {
				putGAM(dst_mode, dst_reg, word_mode, src_value, false);
			}

			setPSW_n(SIGN(src_value, word_mode));
			setPSW_z(src_value == 0);
			setPSW_v(false);

			return true;

		case 0b010: { // CMP/CMPB Compare Word/Byte
				    src_value = getGAM(src_mode, src_reg, word_mode, false);
				    uint16_t dst_value = getGAM(dst_mode, dst_reg, word_mode, false);

				    uint16_t temp = (src_value - dst_value) & (word_mode ? 0xff : 0xffff);
				    setPSW_n(SIGN(temp, word_mode));
				    setPSW_z(temp == 0);

				    setPSW_v(SIGN((src_value ^ dst_value) & (~dst_value ^ temp), word_mode));

				    setPSW_c(src_value < dst_value);

				    return true;
			    }

		case 0b011: { // BIT/BITB Bit Test Word/Byte
				    src_value = getGAM(src_mode, src_reg, word_mode, false);
				    uint16_t dst_value = getGAM(dst_mode, dst_reg, word_mode, false);
				    uint16_t result = dst_value & src_value;
				    setPSW_n(SIGN(result, word_mode));
				    setPSW_z(result == 0);
				    setPSW_v(false);
				    return true;
			    }

		case 0b100: { // BIC/BICB Bit Clear Word/Byte
				    src_value = getGAM(src_mode, src_reg, word_mode, false);
				    uint16_t a = getGAMAddress(dst_mode, dst_reg, word_mode, false);

				    uint16_t result = b -> readWord(a) & ~src_value;

				    if (dst_mode == 0)
					    putGAM(dst_mode, dst_reg, word_mode, result, false);
				    else
					    b -> write(a, word_mode, result);

				    setPSW_n(SIGN(result, word_mode));
				    setPSW_z(result == 0);
				    setPSW_v(false);
				    return true;
			    }

		case 0b101: { // BIS/BISB Bit Set Word/Byte
				    src_value = getGAM(src_mode, src_reg, word_mode, false);
				    uint16_t a = getGAMAddress(dst_mode, dst_reg, word_mode, false);

				    uint16_t result = b -> readWord(a) | src_value;

				    if (dst_mode == 0)
					    putGAM(dst_mode, dst_reg, word_mode, result, false);
				    else
					    b -> write(a, word_mode, result);

				    setPSW_n(SIGN(result, word_mode));
				    setPSW_z(result == 0);
				    setPSW_v(false);
				    return true;
			    }

		case 0b110: { // ADD/SUB Add/Subtract Word
				    int16_t src_value = getGAM(src_mode, src_reg, false, false);
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
				    return true;
			    }
	}

	return false;
}

bool cpu::additional_double_operand_instructions(const uint16_t instr)
{
	const uint8_t reg = (instr >> 6) & 7;

	const uint8_t dst = instr & 63;
	const uint8_t dst_mode = (dst >> 3) & 7;
	const uint8_t dst_reg = dst & 7;

	int operation = (instr >> 9) & 7;

	switch(operation) {
		case 0: { // MUL
				uint16_t R = getRegister(reg);
				int32_t result = R * getGAM(dst_mode, dst_reg, true, false);

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
				int32_t divider = getGAM(dst_mode, dst_reg, true, false);

				if (divider == 0) {
					setPSW_n(false);
					setPSW_z(true);
					setPSW_v(true);
					setPSW_c(true);
				}
				else {
					int32_t quot = R0R1 / divider;
					uint16_t rem = R0R1 % divider;

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
				int16_t  R     = getRegister(reg), oldR = R;
				uint16_t a     = getGAMAddress(dst_mode, dst_reg, false, false);
				int16_t  shift = b->read(a, false);

				if (shift > 0) {
					R <<= shift - 1;
					setPSW_c(R & 0x8000);
					R <<= 1;
				}
				else if (shift < 0) {
					R >>= -(shift - 1);
					setPSW_c(R & 1);
					R >>= 1;
				}

				setPSW_n(R < 0);
				setPSW_z(R == 0);
				setPSW_v(sign(R) != sign(oldR));

				if (dst_mode == 0)
					putGAM(dst_mode, dst_reg, false, R, false);
				else
					b->write(a, false, R);

				return true;
			}

		case 3: { // ASHC
				uint32_t R0R1 = (getRegister(reg) << 16) | getRegister(reg + 1);
				uint16_t a     = getGAMAddress(dst_mode, dst_reg, false, false);
				int16_t  shift = b->read(a, false);

				if (shift > 0) {
					R0R1 <<= (shift & 0b111111) - 1;
					setPSW_c(R0R1 >> 31);
					R0R1 <<= 1;
				}
				else if (shift < 0) {
					R0R1 >>= -((shift & 0b111111) - 1);
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
				uint16_t a = getGAMAddress(dst_mode, dst_reg, false, false);
				uint16_t vl = b->read(a, false) ^ getRegister(reg);

				if (dst_mode == 0)
					putGAM(dst_mode, dst_reg, false, vl, false);
				else
					b->write(a, false, vl);

				setPSW_n(vl & 0x8000);
				setPSW_z(vl == 0);
				setPSW_v(false);

				return true;
			}

		case 7: { // SOB
			uint16_t oldPC = getPC();

			if (reg == 7)
				addRegister(reg, false, -2);
			else
				addRegister(reg, false, -1);

			if (getRegister(reg, false)) {
				uint16_t newPC = oldPC - dst * 2;
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

	switch(opcode) {
		case 0b00000011: // SWAB
			if (word_mode) // handled elsewhere
				return false;
			else {
				a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
				uint8_t t1, t2;
				uint16_t t;
				if (dst_mode == 0) {
					t = getRegister(dst_reg, false);
					t1 = t >> 8;
					t2 = t & 255;
					setRegister(dst_reg, false, (t2 << 8) | t1);
				}
				else {
					t = getRegister(dst_reg, false);
					t1 = b -> readByte(a);
					t2 = b -> readByte(a + 1);

					b -> writeByte(a, t2);
					b -> writeByte(a + 1, t1);
				}

				setPSW_n(t1 & 0x80);
				setPSW_z(t1 == 0);
				setPSW_v(false);
				setPSW_c(false);
				break;
			}

		case 0b000101000: // CLR/CLRB
			a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
			if (dst_mode == 0)
				putGAM(dst_mode, dst_reg, word_mode, 0, false);
			else
				b -> write(a, word_mode, 0);
			setPSW_n(false);
			setPSW_z(true);
			setPSW_v(false);
			setPSW_c(false);
			break;

		case 0b000101001: // COM/COMB
			a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
			v = b -> read(a, word_mode);

			if (word_mode)
				v ^= 0xff;
			else
				v ^= 0xffff;

			setPSW_n(SIGN(v, word_mode));
			setPSW_z(v == 0);
			setPSW_v(false);
			setPSW_c(true);

			if (dst_mode == 0)
				putGAM(dst_mode, dst_reg, word_mode, v, false);
			else
				b -> write(a, word_mode, v);

			break;

		case 0b000101010: // INC/INCB
			a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
			v = b -> read(a, word_mode);
			vl = (v + 1) & (word_mode ? 0xff : 0xffff);
			setPSW_n(word_mode ? vl > 127 : vl > 32767);
			setPSW_z(vl == 0);
			setPSW_v(word_mode ? v == 0x7f : v == 0x7fff);
			if (dst_mode == 0)
				putGAM(dst_mode, dst_reg, word_mode, vl, false);
			else
				b -> write(a, word_mode, vl);
			break;

		case 0b000101011: // DEC/DECB
			a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
			v = b -> read(a, word_mode);
			vl = (v - 1) & (word_mode ? 0xff : 0xffff);
			setPSW_n(word_mode ? vl > 127 : vl > 32767);
			setPSW_z(vl == 0);
			setPSW_v(word_mode ? v == 0x80 : v == 0x8000);
			if (dst_mode == 0)
				putGAM(dst_mode, dst_reg, word_mode, vl, false);
			else
				b -> write(a, word_mode, vl);
			break;

		case 0b000101100: // NEG/NEGB
			a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
			v = b -> read(a, word_mode);
			vl = word_mode ? uint8_t(-int8_t(v)) : -int16_t(v);
			if (dst_mode == 0)
				putGAM(dst_mode, dst_reg, word_mode, vl, false);
			else
				b -> write(a, word_mode, vl);
			setPSW_n(SIGN(vl, word_mode));
			setPSW_z(vl == 0);
			setPSW_v(word_mode ? vl == 0x80 : vl == 0x8000);
			setPSW_c(vl);
			break;

		case 0b000101101: { // ADC/ADCB
					  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
					  uint16_t org = b -> read(a, word_mode);
					  uint16_t new_ = org + getPSW_c();
					  if (dst_mode == 0)
						  putGAM(dst_mode, dst_reg, word_mode, new_, false);
					  else
						  b -> write(a, word_mode, new_);
					  setPSW_n(SIGN(new_, word_mode));
					  setPSW_z(new_ == 0);
					  setPSW_v((word_mode ? org == 0x7f : org == 0x7fff) && getPSW_c());
					  setPSW_c((word_mode ? org == 0xff : org == 0xffff) && getPSW_c());
					  break;
				  }

		case 0b000101110: // SBC/SBCB
				  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
				  //fprintf(stderr, "%d,%d\n", dst_mode, dst_reg);
				  v = b -> read(a, word_mode);
				  vl = (v - getPSW_c()) & (word_mode ? 0xff : 0xffff);
				  if (dst_mode == 0)
					  putGAM(dst_mode, dst_reg, word_mode, vl, false);
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
				  v = getGAM(dst_mode, dst_reg, word_mode, false);
				  setPSW_n(word_mode ? v & 128 : v & 32768);
				  setPSW_z(v == 0);
				  setPSW_v(false);
				  setPSW_c(false);
				  break;

		case 0b000110000: { // ROR/RORB
					  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
					  uint16_t t = b -> read(a, word_mode);
					  bool new_carry = t & 1;

					  uint16_t temp = 0;
					  if (word_mode) {
						  temp = (t >> 1) | (getPSW_c() << 7);
						  if (dst_mode == 0)
							  putGAM(dst_mode, dst_reg, word_mode, temp, false);
						  else
							  b -> writeByte(a, temp);
					  }
					  else {
						  temp = (t >> 1) | (getPSW_c() << 15);
						  if (dst_mode == 0)
							  putGAM(dst_mode, dst_reg, word_mode, temp, false);
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
					  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
					  uint16_t t = b -> read(a, word_mode);
					  bool new_carry = false;

					  uint16_t temp = 0;
					  if (word_mode) {
						  new_carry = t & 0x80;
						  temp = ((t << 1) | getPSW_c()) & 0xff;
						  if (dst_mode == 0)
							  putGAM(dst_mode, dst_reg, word_mode, temp, false);
						  else
							  b -> writeByte(a, temp);
					  }
					  else {
						  new_carry = t & 0x8000;
						  temp = (t << 1) | getPSW_c();
						  if (dst_mode == 0)
							  putGAM(dst_mode, dst_reg, word_mode, temp, false);
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
						  putGAM(dst_mode, dst_reg, word_mode, vl, false);
					  else
						  b -> write(a, word_mode, vl);

					  setPSW_n(SIGN(vl, word_mode));
					  setPSW_z(vl == 0);
					  setPSW_v(getPSW_n() ^ getPSW_c());
					  break;
				  }

		case 0b00110011: // ASL/ASLB
				  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
				  vl = b -> read(a, word_mode);
				  v = (vl << 1) & (word_mode ? 0xff : 0xffff);
				  setPSW_n(word_mode ? v & 0x80 : v & 0x8000);
				  setPSW_z(v == 0);
				  setPSW_c(word_mode ? vl & 0x80 : vl & 0x8000);
				  setPSW_v(getPSW_n() ^ getPSW_c());
				  if (dst_mode == 0)
					  putGAM(dst_mode, dst_reg, word_mode, v, false);
				  else
					  b -> write(a, word_mode, v);
				  break;

		case 0b00110101: // MFPD/MFPI
				  // FIXME
				  v = getGAM(dst_mode, dst_reg, word_mode, true);
				  setPSW_n(word_mode ? v & 0x80 : v & 0x8000);
				  setPSW_z(v == 0);
				  setPSW_v(false);
				  pushStack(v);
				  break;

		case 0b00110110: // MTPI/MTPD
				  // FIXME
				  a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
				  v = popStack();
				  setPSW_n(word_mode ? v & 0x80 : v & 0x8000);
				  setPSW_z(v == 0);
				  setPSW_v(false);
				  if (dst_mode == 0)
					  putGAM(dst_mode, dst_reg, word_mode, v, true);
				  else
					  b -> write(a, word_mode, v); // ?
				  break;

		case 0b000110100: // MTPS (put something in PSW)
				  psw = getGAM(dst_mode, dst_reg, word_mode, false);
				  break;

		case 0b000110111: // MFPS (get PSW to something) / SXT
				  if (word_mode) {  // MFPS
				  	putGAM(dst_mode, dst_reg, word_mode, psw, false);
				  }
				  else {  // SXT
					a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
					v = b -> read(a, word_mode);

					vl = getPSW_n() ? -1 : 0;

					setPSW_z(getPSW_n() == false);
					setPSW_v(false);

					if (dst_mode == 0)
						putGAM(dst_mode, dst_reg, word_mode, vl, false);
					else
						b -> write(a, word_mode, vl);
				  }
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

	switch(opcode) {
		case 0b00000001: // BR
			take = true;
			break;

		case 0b00000010: // BNE
			take = !getPSW_z();
			break;

		case 0b00000011: // BEQ
			take = getPSW_z();
			break;

		case 0b00000100: // BGE
			take = (getPSW_n() ^ getPSW_v()) == false;
			break;

		case 0b00000101: // BLT
			take = getPSW_n() ^ getPSW_v();
			break;

		case 0b00000110: // BGT
			take = ((getPSW_n() ^ getPSW_v()) | getPSW_z()) == false;
			break;

		case 0b00000111: // BLE
			take = (getPSW_n() ^ getPSW_v()) | getPSW_z();
			break;

		case 0b10000000: // BPL
			take = getPSW_n() == false;
			break;

		case 0b10000001: // BMI
			take = getPSW_n() == true;
			break;

		case 0b10000010: // BHI
			take = getPSW_c() == false && getPSW_z() == false;
			break;

		case 0b10000011: // BLOS
			take = getPSW_c() | getPSW_z();
			break;

		case 0b10000100: // BVC
			take = getPSW_v() == false;
			break;

		case 0b10000101: // BVS
			take = getPSW_v();
			break;

		case 0b10000110: // BCC
			take = getPSW_c() == false;
			break;

		case 0b10000111: // BCS / BLO
			take = getPSW_c();
			break;

		default:
			return false;
	}

	if (take)
		addRegister(7, false, offset * 2);

	return true;
}

bool cpu::condition_code_operations(const uint16_t instr)
{
	switch(instr) {
		case 0b0000000010100000: // NOP
		case 0b0000000010110000: // NOP
			return true;
	}

	if ((instr & ~7) == 0000230) { // SPLx
		int level = instr & 7;
		setPSW_spl(level);

		// trap via vector 010
		trap(010);

		fprintf(stderr, "SPL%d, new pc: %06o\n", level, getPC());

		return true;
	}

	if ((instr & ~31) == 0b10100000) { // set condition bits
		bool state = !!(instr & 0b10000);

		if (instr & 0b1000)
			setPSW_n(state);
		if (instr & 0b0100)
			setPSW_z(state);
		if (instr & 0b0010)
			setPSW_v(state);
		if (instr & 0b0001)
			setPSW_c(state);

		return true;
	}

	return false;
}

void cpu::pushStack(const uint16_t v)
{
	if (getRegister(6) == stackLimitRegister) {
		printf("stackLimitRegister reached\n");  // TODO
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

bool cpu::misc_operations(const uint16_t instr)
{
	switch(instr) {
		case 0b0000000000000000: // HALT
			// pretend HALT is not executed, proceed
			haltFlag = true;
			return true;

		case 0b0000000000000001: // WAIT
			return true;

		case 0b0000000000000010: // RTI
			setPC(popStack());
			setPSW(popStack());
			return true;

		case 0b0000000000000011: // BPT
			trap(014);
			return true;

		case 0b0000000000000100: // IOT
			trap(020);
			return true;

		case 0b0000000000000110: // RTT
			setPC(popStack());
			setPSW(popStack());
			return true;

		case 0b0000000000000111: // MFPT
			if (emulateMFPT)
				setRegister(0, true, 1); // PDP-11/44
			else
				trap(012);
			return true;

		case 0b0000000000000101: // RESET
			resetFlag = true;
			return true;
	}

	if ((instr >> 8) == 0b10001000) { // EMT
		trap(030);
		return true;
	}

	if ((instr >> 8) == 0b10001001) { // TRAP
		trap(034);
		return true;
	}

	if ((instr & ~0b111111) == 0b0000000001000000) { // JMP
		int dst_mode = (instr >> 3) & 7, dst_reg = instr & 7;
		bool word_mode = false;
		setPC(getGAMAddress(dst_mode, dst_reg, word_mode, false));
		return true;
	}

	if ((instr & 0b1111111000000000) == 0b0000100000000000) { // JSR
		const int link_reg = (instr >> 6) & 7;
		uint16_t dst_value = getGAMAddress((instr >> 3) & 7, instr & 7, false, false);

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

		return true;
	}

	return false;
}

void cpu::busError()
{
	trap(4);
}

void cpu::trap(const uint16_t vector)
{
	uint16_t before_psw = getPSW();
	uint16_t before_pc  = getPC();

	// switch to kernel mode & update 'previous mode'
	uint16_t new_psw = b->readWord(vector + 2) & 0147777;  // mask off old 'previous mode'
	new_psw |= (before_psw >> 2) & 030000; // apply new 'previous mode'
	setPSW(new_psw);

	pushStack(before_psw);
	pushStack(before_pc);

	setPC(b->readWord(vector + 0));

	D(fprintf(stderr, "TRAP %o: PC is now %06o, PSW is now %06o\n", vector, getPC(), new_psw);)
}

std::pair<std::string, int> cpu::addressing_to_string(const uint8_t mode_register, const uint16_t pc)
{
#if !defined(ESP32)
	assert(mode_register < 64);

	int         pc_offset = 0;

	uint16_t    next_word = b->readWord(pc & 65535);

	int         reg       = mode_register & 7;

	std::string reg_name;
	if (reg == 6)
		reg_name = "SP";
	else if (reg == 7)
		reg_name = "PC";
	else
		reg_name = format("R%d", reg);

	switch(mode_register >> 3) {
		case 0:
			return { reg_name, 2 };

		case 1:
			return { format("(%s)", reg_name.c_str()), 2 };

		case 2:
			if (reg == 7)
				return { format("#%06o", next_word), 4 };

			return { format("(%s)+", reg_name.c_str()), 2 };

		case 3:
			if (reg == 7)
				return { format("@#%06o", next_word), 4 };

			return { format("@(%s)+", reg_name.c_str()), 2 };

		case 4:
			return { format("-(%s)", reg_name.c_str()), 2 };

		case 5:
			return { format("@-(%s)", reg_name.c_str()), 2 };

		case 6:
			if (reg == 7)
				return { format("%06o", (pc + next_word + 2) & 65535), 4 };

			return { format("o%o(%s)", next_word, reg_name.c_str()), 4 };

		case 7:
			if (reg == 7)
				return { format("@%06o", next_word), 4 };

			return { format("@o%o(%s)", next_word, reg_name.c_str()), 4 };
	}
#endif
	return { "??", 0 };
}

void cpu::disassemble()
{
#if !defined(ESP32)
	uint16_t    pc            = getPC();
	uint16_t    instruction   = b->readWord(pc);

	bool        word_mode     = !!(instruction & 0x8000);
	std::string word_mode_str = word_mode ? "B" : "";
	uint8_t     ado_opcode    = (instruction >>  9) &  7;  // additional double operand
        uint8_t     do_opcode     = (instruction >> 12) &  7;  // double operand
	uint8_t     so_opcode     = (instruction >>  6) & 63;  // single operand

	std::string text;
	std::string name;

	std::string space = " ";
	std::string comma = ",";

	uint8_t     src_register  = (instruction >> 6) & 63;
	uint8_t     dst_register  = (instruction >> 0) & 63;

	// TODO: 100000011

	if (do_opcode == 0b000) {
		auto dst_text = addressing_to_string(dst_register, pc);

		// single_operand_instructions
		switch(so_opcode) {
			case 0b00000011:
				text = "SWAB " + dst_text.first;
				break;

			case 0b000101000:
				name = "CLR";
				break;

			case 0b000101001:
				name = "COM";
				break;

			case 0b000101010:
				name = "INC";
				break;

			case 0b000101011:
				name = "DEC";
				break;

			case 0b000101100:
				name = "NEG";
				break;

			case 0b000101101:
				name = "ADC";
				break;

			case 0b000101110:
				name = "SBC";
				break;

			case 0b000101111:
				name = "TST";
				break;

			case 0b000110000:
				name = "ROR";
				break;

			case 0b000110001:
				name = "ROL";
				break;

			case 0b000110010:
				name = "ASR";
				break;

			case 0b00110011:
				name = "ASL";
				break;

			case 0b00110101:
				name = word_mode ? "MFPD" : "MFPI";
				break;

			case 0b00110110:
				name = word_mode ? "MTPD" : "MTPI";
				break;

			case 0b000110100:
				if (word_mode == false)
					name = "MTPS";
				break;

			case 0b000110111:
				if (word_mode == false)
					name = "SXT";
				else
					name = "MFPS";
				break;
		}

		if (text.empty() && name.empty() == false)
			text = name + word_mode_str + space + dst_text.first;
	}
	else if (do_opcode == 0b111) {
		std::string src_text = format("R%d", (instruction >> 6) & 7);
		auto        dst_text = addressing_to_string(dst_register, pc);
		uint8_t     dst      = instruction & 63;

		switch(ado_opcode) {  // additional double operand
			case 0:
				name = "MUL";
				break;

			case 1:
				name = "DIV";
				break;

			case 2:
				name = "ASH";
				break;

			case 3:
				name = "ASHC";
				break;

			case 4:
				name = "XOR";
				break;

			case 7:
				text = std::string("SOB ") + src_text;
				break;
		}

		if (text.empty() && name.empty() == false)
			text = name + space + src_text + comma + dst_text.first;
	}
	else {
		switch(do_opcode) {
			case 0b001:
				name = "MOV";
				break;

			case 0b010:
				name = "CMP";
				break;

			case 0b011:
				name = "BIT";
				break;

			case 0b100:
				name = "BIC";
				break;

			case 0b101:
				name = "BIS";
				break;

			case 0b110:
				if (word_mode)
					name = "SUB";
				else
					name = "ADD";
				break;
		}

		auto src_text = addressing_to_string(src_register, (pc + 2) & 65535);
		auto dst_text = addressing_to_string(dst_register, (pc + src_text.second) & 65535);

		text = name + word_mode_str + space + src_text.first + comma + dst_text.first;
	}

	if (text.empty()) {  // conditional branch instructions
		uint8_t  cb_opcode = (instruction >> 8) & 255;
		int8_t   offset    = instruction & 255;
		uint16_t new_pc    = (pc + 2 + offset * 2) & 65535;

		switch(cb_opcode) {
			case 0b00000001:
				name = "BR";
				break;

			case 0b00000010:
				name = "BNE";
				break;

			case 0b00000011:
				name = "BEQ";
				break;

			case 0b00000100:
				name = "BGE";
				break;

			case 0b00000101:
				name = "BLT";
				break;

			case 0b00000110:
				name = "BGT";
				break;

			case 0b00000111:
				name = "BLE";
				break;

			case 0b10000000:
				name = "BPL";
				break;

			case 0b10000001:
				name = "BMI";
				break;

			case 0b10000010:
				name = "BHI";
				break;

			case 0b10000011:
				name = "BLOS";
				break;

			case 0b10000100:
				name = "BVC";
				break;

			case 0b10000101:
				name = "BVS";
				break;

			case 0b10000110:
				name = "BCC";
				break;

			case 0b10000111:
				name = "BCS/BLO";
				break;
		}

		if (text.empty() && name.empty() == false)
			text = name + space + format("%06o", new_pc);
	}

	if (text.empty()) {
		if ((instruction & ~7) == 0000230)
			text = format("SPL%d", instruction & 7);

		if ((instruction & ~31) == 0b10100000) { // set condition bits
			text = instruction & 0b10000 ? "SE" : "CL";

			if (instruction & 0b1000)
				text += "N";
			if (instruction & 0b0100)
				text += "Z";
			if (instruction & 0b0010)
				text += "V";
			if (instruction & 0b0001)
				text += "C";
		}

		switch(instruction) {
			case 0b0000000010100000:
			case 0b0000000010110000:
				text = "NOP";
				break;

			case 0b0000000000000000:
				text = "HALT";
				break;

			case 0b0000000000000001:
				text = "WAIT";
				break;

			case 0b0000000000000010:
				text = "RTI";
				break;

			case 0b0000000000000011:
				text = "BPT";
				break;

			case 0b0000000000000100:
				text = "IOT";
				break;

			case 0b0000000000000110:
				text = "RTT";
				break;

			case 0b0000000000000111:
				text = "MFPT";
				break;

			case 0b0000000000000101:
				text = "RESET";
				break;
		}

		if ((instruction >> 8) == 0b10001000)
			text = format("EMT %o", instruction & 255);

		if ((instruction >> 8) == 0b10001001)
			text = format("TRAP %o", instruction & 255);

		if ((instruction & ~0b111111) == 0b0000000001000000) {
			auto dst_text = addressing_to_string(src_register, pc);

			text = std::string("JMP ") + dst_text.first;
		}

		if ((instruction & 0b1111111000000000) == 0b0000100000000000) {
			auto dst_text = addressing_to_string(src_register, pc);

			text = std::string("JSR ") + dst_text.first;
		}

		if ((instruction & 0b1111111111111000) == 0b0000000010000000)
			text = "RTS";
	}

	if (text.empty())
		text = "???";

	fprintf(stderr, "R0: %06o, R1: %06o, R2: %06o, R3: %06o, R4: %06o, R5: %06o, SP: %06o, PC: %06o, PSW: %d%d|%d|%d|%d%d%d%d%d, instr: %06o: %s\n",
			getRegister(0), getRegister(1), getRegister(2), getRegister(3), getRegister(4), getRegister(5),
			sp[psw >> 14], pc,
			psw >> 14, (psw >> 12) & 3, (psw >> 11) & 1, (psw >> 5) & 7, !!(psw & 16), !!(psw & 8), !!(psw & 4), !!(psw & 2), psw & 1,
			instruction, text.c_str());
#endif
}

bool cpu::step()
{
	if (getPC() & 1)
		busError();

	disassemble();

	b -> setMMR2(getPC());
	uint16_t instr = b->readWord(getPC());
	addRegister(7, false, 2);

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
		if (fh) {
			for(int i=0; i<256; i++)
				fputc(b -> readByte(getPC() - 2 + i), fh);
			fclose(fh);
		}
	}
	busError();
	exit(1);
	return false;

ok:
	return haltFlag; // return flags that indicate that special attention is required
}
