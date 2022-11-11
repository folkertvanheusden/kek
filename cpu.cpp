// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "gen.h"
#include "log.h"
#include "utils.h"

#define SIGN(x, wm) ((wm) ? (x) & 0x80 : (x) & 0x8000)

#define IS_0(x, wm) ((wm) ? ((x) & 0xff) == 0 : (x) == 0)

cpu::cpu(bus *const b, std::atomic_uint32_t *const event) : b(b), event(event)
{
	reset();
}

cpu::~cpu()
{
}

void cpu::init_interrupt_queue()
{
	queued_interrupts.clear();

	for(int level=0; level<8; level++)
		queued_interrupts.insert({ level, { } });
}

void cpu::emulation_start()
{
	instruction_count = 0;

	running_since = get_ms();
}

bool cpu::check_breakpoint()
{
	return breakpoints.find(getPC()) != breakpoints.end();
}

void cpu::set_breakpoint(const uint16_t addr)
{
	breakpoints.insert(addr);
}

void cpu::remove_breakpoint(const uint16_t addr)
{
	breakpoints.erase(addr);
}

std::set<uint16_t> cpu::list_breakpoints()
{
	return breakpoints;
}

uint64_t cpu::get_instructions_executed_count()
{
	// this may wreck havoc as it is not protected by a mutex
	// but a mutex would slow things down too much (as would
	// do an atomic)
	return instruction_count;
}

std::tuple<double, double, uint64_t> cpu::get_mips_rel_speed()
{
	uint64_t instr_count = get_instructions_executed_count();

        uint32_t t_diff = get_ms() - running_since;

        double mips = instr_count / (1000.0 * t_diff);

        // see https://retrocomputing.stackexchange.com/questions/6960/what-was-the-clock-speed-and-ips-for-the-original-pdp-11
        constexpr double pdp11_clock_cycle = 150;  // ns, for the 11/70
        constexpr double pdp11_mhz = 1000.0 / pdp11_clock_cycle;
        constexpr double pdp11_avg_cycles_per_instruction = (1 + 5) / 2.0;
        constexpr double pdp11_estimated_mips = pdp11_mhz / pdp11_avg_cycles_per_instruction;

	return { mips, mips * 100 / pdp11_estimated_mips, instr_count };
}

void cpu::reset()
{
	memset(regs0_5, 0x00, sizeof regs0_5);
	memset(sp, 0x00, sizeof sp);
	pc = 0;
	psw = 7 << 5;
	fpsr = 0;
	runMode = false;
	init_interrupt_queue();
}

uint16_t cpu::getRegister(const int nr, const int mode, const bool sp_prev_mode) const
{
	if (nr < 6)
		return regs0_5[mode][nr];

	if (nr == 6) {
		if (sp_prev_mode)
			return sp[(getPSW() >> 12) & 3];

		return sp[getPSW() >> 14];
	}

	return pc;
}

uint16_t cpu::getRegister(const int nr) const
{
	if (nr < 6)
		return regs0_5[getBitPSW(11)][nr];

	if (nr == 6)
		return sp[getPSW() >> 14];

	return pc;
}

void cpu::setRegister(const int nr, const bool set, const bool prev_mode, const uint16_t value)
{
	if (nr < 6)
		regs0_5[set][nr] = value;
	else if (nr == 6) {
		if (prev_mode)
			sp[(getPSW() >> 12) & 3] = value;
		else
			sp[getPSW() >> 14] = value;
	}
	else {
		pc = value;
	}
}

void cpu::setRegisterLowByte(const int nr, const bool word_mode, const uint16_t value)  // prev_mode == false
{
	if (word_mode) {
		uint16_t v = getRegister(nr);

		v &= 0xff00;

		assert(value < 256);
		v |= value;

		setRegister(nr, v);
	}
	else {
		setRegister(nr, value);
	}
}

bool cpu::put_result(const uint16_t a, const uint8_t dst_mode, const uint8_t dst_reg, const bool word_mode, const uint16_t value)
{
	if (dst_mode == 0) {
		setRegisterLowByte(dst_reg, word_mode, value);

		return true;
	}

	b->write(a, word_mode, value, false);
	
	return a != ADDR_PSW;
}

uint16_t cpu::addRegister(const int nr, const bool prev_mode, const uint16_t value)
{
	if (nr < 6)
		return regs0_5[getBitPSW(11)][nr] += value;

	if (nr == 6) {
		if (prev_mode)
			return sp[(getPSW() >> 12) & 3] += value;

		return sp[getPSW() >> 14] += value;
	}

	return pc += value;
}

bool cpu::getBitPSW(const int bit) const
{
	return (psw >> bit) & 1;
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
	psw &= ~(7 << 5);
	psw |= v << 5;
}

int cpu::getPSW_spl() const
{
	return (psw >> 5) & 7;
}

void cpu::setPSW(uint16_t v, const bool limited)
{
	if (limited) {
		v &= 0174037;

		v |= psw & 0174340;
	}

	psw = v;
}

bool cpu::check_queued_interrupts()
{
	std::unique_lock<std::mutex> lck(qi_lock);

	uint8_t current_level = getPSW_spl();

	// uint8_t start_level = current_level <= 3 ? 0 : current_level + 1;
	uint8_t start_level = current_level + 1;

	for(uint8_t i=start_level; i < 8; i++) {
		auto interrupts = queued_interrupts.find(i);

		if (interrupts->second.empty() == false) {
			auto    vector = interrupts->second.begin();

			uint8_t v      = *vector;

			interrupts->second.erase(vector);

			DOLOG(debug, true, "Invoking interrupt vector %o (IPL %d, current: %d)", v, i, current_level);

			trap(v, i, true);

			return true;
		}
	}
	
	return false;
}

void cpu::queue_interrupt(const uint8_t level, const uint8_t vector)
{
	std::unique_lock<std::mutex> lck(qi_lock);

	auto it = queued_interrupts.find(level);

	it->second.insert(vector);

	DOLOG(debug, true, "Queueing interrupt vector %o (IPL %d, current: %d), n: %zu", vector, level, getPSW_spl(), it->second.size());
}

void cpu::addToMMR1(const uint8_t mode, const uint8_t reg, const bool word_mode)
{
	if (mode == 0 || mode == 1 || (b->getMMR0() & 0160000 /* bits frozen? */))
		return;

	bool neg = mode == 4 || mode == 5;

	if (!word_mode || reg >= 6 || mode == 6 || mode == 7)
		b->addToMMR1(neg ? -2 : 2, reg);
	else
		b->addToMMR1(neg ? -1 : 1, reg);
}

// GAM = general addressing modes
uint16_t cpu::getGAM(const uint8_t mode, const uint8_t reg, const bool word_mode, const bool prev_mode)
{
	uint16_t next_word = 0;
	uint16_t temp      = 0;

	int      set       = getBitPSW(11);

	switch(mode) {
		case 0: // 000 
			return getRegister(reg, set, prev_mode) & (word_mode ? 0xff : 0xffff);
		case 1:
			return b -> read(getRegister(reg, set, prev_mode), word_mode, prev_mode);
		case 2:
			temp = b -> read(getRegister(reg, set, prev_mode), word_mode, prev_mode);
			addRegister(reg, prev_mode, !word_mode || reg == 7 || reg == 6 ? 2 : 1);
			return temp;
		case 3:
			temp = b -> read(b -> read(getRegister(reg, set, prev_mode), false, prev_mode), word_mode, prev_mode);
			addRegister(reg, prev_mode, 2);
			return temp;
		case 4:
			addRegister(reg, prev_mode, !word_mode || reg == 7 || reg == 6 ? -2 : -1);
			return b -> read(getRegister(reg, set, prev_mode), word_mode, prev_mode);
		case 5:
			addRegister(reg, prev_mode, -2);
			return b -> read(b -> read(getRegister(reg, set, prev_mode), false, prev_mode), word_mode, prev_mode);
		case 6:
			next_word = b -> read(getPC(), false, prev_mode);
			addRegister(7, prev_mode, + 2);
			temp = b -> read(getRegister(reg, set, prev_mode) + next_word, word_mode, prev_mode);
			return temp;
		case 7:
			next_word = b -> read(getPC(), false, prev_mode);
			addRegister(7, prev_mode, + 2);
			return b -> read(b -> read(getRegister(reg, set, prev_mode) + next_word, false, prev_mode), word_mode, prev_mode);
	}

	return -1;
}

bool cpu::putGAM(const uint8_t mode, const int reg, const bool word_mode, const uint16_t value, bool const prev_mode)
{
	uint16_t next_word = 0;
	int      addr      = -1;

	int      set       = getBitPSW(11);

	uint16_t dummy = 0;

	switch(mode) {
		case 0:
			setRegister(reg, prev_mode, value);
			break;
		case 1:
			addr = getRegister(reg, set, prev_mode);
			b -> write(addr, word_mode, value, false);
			break;
		case 2:
			addr = getRegister(reg, set, prev_mode);
			b -> write(addr, word_mode, value, false);
			addRegister(reg, prev_mode, !word_mode || reg == 7 || reg == 6 ? 2 : 1);
			break;
		case 3:
			addr = b -> readWord(getRegister(reg, set, prev_mode));
			b -> write(addr, word_mode, value, false);
			addRegister(reg, prev_mode, 2);
			break;
		case 4:
			dummy = addRegister(reg, prev_mode, !word_mode || reg == 7 || reg == 6 ? -2 : -1);
			b -> write(dummy, word_mode, value, false);
			break;
		case 5:
			addRegister(reg, prev_mode, -2);
			addr = b -> readWord(getRegister(reg, set, prev_mode));
			b -> write(addr, word_mode, value, false);
			break;
		case 6:
			next_word = b -> readWord(getPC());
			addRegister(7, prev_mode, 2);
			addr = (getRegister(reg, set, prev_mode) + next_word) & 0xffff;
			b -> write(addr, word_mode, value, false);
			break;
		case 7:
			next_word = b -> readWord(getPC());
			addRegister(7, prev_mode, 2);
			addr = b -> readWord(getRegister(reg, set, prev_mode) + next_word);
			b -> write(addr, word_mode, value, false);
			break;

		default:
			// error
			break;
	}

	return addr == -1 || addr != ADDR_PSW;
}

uint16_t cpu::getGAMAddress(const uint8_t mode, const int reg, const bool word_mode, const bool prev_mode)
{
	uint16_t next_word = 0;
	uint16_t temp      = 0;

	int      set       = getBitPSW(11);

	switch(mode) {
		case 0:
			// registers are also mapped in memory
			return 0177700 + reg;
		case 1:
			return getRegister(reg, set, prev_mode);
		case 2:
			temp = getRegister(reg, set, prev_mode);
			addRegister(reg, prev_mode, !word_mode || reg == 6 || reg == 7 ? 2 : 1);
			return temp;
		case 3:
			temp = b -> readWord(getRegister(reg, set, prev_mode));
			addRegister(reg, prev_mode, 2);
			return temp;
		case 4:
			addRegister(reg, prev_mode, !word_mode || reg == 6 || reg == 7 ? -2 : -1);
			return getRegister(reg, set, prev_mode);
		case 5:
			addRegister(reg, prev_mode, -2);
			return b -> readWord(getRegister(reg, set, prev_mode));
		case 6:
			next_word = b -> readWord(getPC());
			addRegister(7, prev_mode, 2);
			return getRegister(reg, set, prev_mode) + next_word;
		case 7:
			next_word = b -> readWord(getPC());
			addRegister(7, prev_mode, 2);
			return b -> readWord(getRegister(reg, set, prev_mode) + next_word);
	}

	return -1;
}

bool cpu::double_operand_instructions(const uint16_t instr)
{
	const bool    word_mode = !!(instr & 0x8000);

	const uint8_t operation = (instr >> 12) & 7;

	if (operation == 0b000)
		return single_operand_instructions(instr);

	if (operation == 0b111)
		return additional_double_operand_instructions(instr);

	const uint8_t src        = (instr >> 6) & 63;
	const uint8_t src_mode   = (src >> 3) & 7;
	const uint8_t src_reg    = src & 7;

	const uint16_t src_value = operation == 0b110 ? 0 : getGAM(src_mode, src_reg, word_mode, false);

	const uint8_t dst        = instr & 63;
	const uint8_t dst_mode   = (dst >> 3) & 7;
	const uint8_t dst_reg    = dst & 7;

	bool set_flags  = true;

	switch(operation) {
		case 0b001: { // MOV/MOVB Move Word/Byte
				    addToMMR1(src_mode, src_reg, word_mode);

				    if (word_mode && dst_mode == 0)
					    setRegister(dst_reg, false, int8_t(src_value));  // int8_t: sign extension
				    else
					    set_flags = putGAM(dst_mode, dst_reg, word_mode, src_value, false);

				    addToMMR1(dst_mode, dst_reg, word_mode);

				    if (set_flags) {
					    setPSW_n(SIGN(src_value, word_mode));
					    setPSW_z(IS_0(src_value, word_mode));
					    setPSW_v(false);
				    }

				    return true;
			    }

		case 0b010: { // CMP/CMPB Compare Word/Byte
				    addToMMR1(src_mode, src_reg, word_mode);

				    uint16_t dst_value = getGAM(dst_mode, dst_reg, word_mode, false);

				    uint16_t temp      = (src_value - dst_value) & (word_mode ? 0xff : 0xffff);

				    addToMMR1(dst_mode, dst_reg, word_mode);

				    setPSW_n(SIGN(temp, word_mode));
				    setPSW_z(IS_0(temp, word_mode));
				    setPSW_v(SIGN((src_value ^ dst_value) & (~dst_value ^ temp), word_mode));
				    setPSW_c(src_value < dst_value);

				    return true;
			    }

		case 0b011: { // BIT/BITB Bit Test Word/Byte
				    uint16_t dst_value = getGAM(dst_mode, dst_reg, word_mode, false);
				    uint16_t result    = (dst_value & src_value) & (word_mode ? 0xff : 0xffff);

				    setPSW_n(SIGN(result, word_mode));
				    setPSW_z(IS_0(result, word_mode));
				    setPSW_v(false);

				    return true;
			    }

		case 0b100: { // BIC/BICB Bit Clear Word/Byte
				    uint16_t a      = getGAMAddress(dst_mode, dst_reg, word_mode, false);

				    uint16_t result = b->read(a, word_mode, false) & ~src_value;

				    if (put_result(a, dst_mode, dst_reg, word_mode, result)) {
					    setPSW_n(SIGN(result, word_mode));
					    setPSW_z(IS_0(result, word_mode));
					    setPSW_v(false);
				    }

				    return true;
			    }

		case 0b101: { // BIS/BISB Bit Set Word/Byte
				    uint16_t a      = getGAMAddress(dst_mode, dst_reg, word_mode, false);

				    uint16_t result = b->read(a, word_mode, false) | src_value;

				    if (put_result(a, dst_mode, dst_reg, word_mode, result)) {
					    setPSW_n(SIGN(result, word_mode));
					    setPSW_z(IS_0(result, word_mode));
					    setPSW_v(false);
				    }

				    return true;
			    }

		case 0b110: { // ADD/SUB Add/Subtract Word
				    int16_t  ssrc_value = getGAM(src_mode, src_reg, false, false);

				    uint16_t dst_addr   = getGAMAddress(dst_mode, dst_reg, false, false);
				    int16_t  dst_value  = b->readWord(dst_addr);
				    int16_t  result     = 0;

				    bool     set_flags  = dst_addr != ADDR_PSW;

				    if (instr & 0x8000) {
					    result = (dst_value - ssrc_value) & 0xffff;

					    if (set_flags) {
						    setPSW_v(sign(ssrc_value) != sign(dst_value) && sign(ssrc_value) == sign(result));
						    setPSW_c(uint16_t(dst_value) < uint16_t(ssrc_value));
					    }
				    }
				    else {
					    result = (dst_value + ssrc_value) & 0xffff;

					    if (set_flags) {
						    setPSW_v(sign(ssrc_value) == sign(dst_value) && sign(dst_value) != sign(result));
						    setPSW_c(uint16_t(result) < uint16_t(ssrc_value));
					    }
				    }

				    if (set_flags) {
					    setPSW_n(result < 0);
					    setPSW_z(result == 0);
				    }

				    if (dst_mode == 0)
					    setRegister(dst_reg, false, result);
				    else
					    b->writeWord(dst_addr, result);

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
				int16_t R1     = getRegister(reg);
				int16_t R2     = getGAM(dst_mode, dst_reg, true, false);
				int32_t result = R1 * R2;

				setRegister(reg, result >> 16);
				setRegister(reg | 1, result & 65535);

				setPSW_n(result < 0);
				setPSW_z(result == 0);
				setPSW_v(false);
				setPSW_c(result < -32768 || result > 32767);
				return true;
			}

		case 1: { // DIV
				int16_t divider = getGAM(dst_mode, dst_reg, false, false);

				if (divider == 0) {  // divide by zero
					setPSW_n(false);
					setPSW_z(true);
					setPSW_v(true);
					setPSW_c(true);

					return true;
				}

				int32_t R0R1    = (getRegister(reg) << 16) | getRegister(reg | 1);

				int32_t  quot   = R0R1 / divider;
				uint16_t rem    = R0R1 % divider;

				// TODO: handle results out of range

				setPSW_n(quot < 0);
				setPSW_z(quot == 0);
				setPSW_c(false);

				if (quot > 32767 || quot < -32768) {
					setPSW_v(true);

					return true;
				}

				setRegister(reg, quot);
				setRegister(reg | 1, rem);

				setPSW_v(false);

				return true;
			}

		case 2: { // ASH
				uint32_t R     = getRegister(reg), oldR = R;
				uint16_t shift = getGAM(dst_mode, dst_reg, false, false) & 077;
				bool     sign  = SIGN(R, false);

				// extend sign-bit
				if (sign)
					R |= 0xffff0000;
				
				if (shift == 0) {
					setPSW_c(false);
				}
				else if (shift < 32) {
					R <<= shift;
					setPSW_c(R & 0x10000);
				}
				else if (shift == 32) {
					R = -sign;

					setPSW_c(sign);
				}
				else {
					int shift_n = (64 - shift) - 1;

					R >>= shift_n;

					setPSW_c(R & 1);

					R >>= 1;
				}

				R &= 0xffff;

				setPSW_n(SIGN(R, false));
				setPSW_z(R == 0);
				setPSW_v(SIGN(R, false) != SIGN(oldR, false));

				setRegister(reg, R);

				return true;
			}

		case 3: { // ASHC
				uint32_t R0R1  = (getRegister(reg) << 16) | getRegister(reg | 1);
				uint16_t shift = getGAM(dst_mode, dst_reg, false, false) & 077;
				bool     sign  = R0R1 & 0x80000000;

				setPSW_v(false);

				if (shift == 0)
					setPSW_c(false);
				else if (shift < 32) {
					R0R1 <<= shift - 1;

					setPSW_c(R0R1 >> 31);

					R0R1 <<= 1;
				}
				else if (shift == 32) {
					R0R1 = -sign;

					setPSW_c(sign);
				}
				else {
					int shift_n = (64 - shift) - 1;

					// extend sign-bit
					if (sign)  // convert to unsigned 64b int & extend sign
					{
						R0R1 = (uint64_t(R0R1) | 0xffffffff00000000ll) >> shift_n;

						setPSW_c(R0R1 & 1);

						R0R1 = (uint64_t(R0R1) | 0xffffffff00000000ll) >> 1;
					}
					else {
						R0R1 >>= shift_n;

						setPSW_c(R0R1 & 1);

						R0R1 >>= 1;
					}
				}

				bool new_sign = R0R1 & 0x80000000;
				setPSW_v(sign != new_sign);

				setRegister(reg, R0R1 >> 16);
				setRegister(reg | 1, R0R1 & 65535);

				setPSW_n(R0R1 & 0x80000000);
				setPSW_z(R0R1 == 0);

				return true;
			}

		case 4: { // XOR (word only)
				uint16_t a         = getGAMAddress(dst_mode, dst_reg, false, false);
				uint16_t vl        = b->read(a, false, false) ^ getRegister(reg);
				bool     set_flags = true;

				if (dst_mode == 0)
					putGAM(dst_mode, dst_reg, false, vl, false);
				else {
					b->write(a, false, vl, false);

					set_flags = a != ADDR_PSW;
				}

				if (set_flags) {
					setPSW_n(vl & 0x8000);
					setPSW_z(vl == 0);
					setPSW_v(false);
				}

				return true;
			}

		case 7: { // SOB
				addRegister(reg, false, -1);

				if (getRegister(reg)) {
					uint16_t newPC = getPC() - dst * 2;

					setPC(newPC);
				}

				return true;
			}
	}

	return false;
}

bool cpu::single_operand_instructions(const uint16_t instr)
{
	const uint16_t opcode    = (instr >> 6) & 0b111111111;
	const uint8_t  dst       = instr & 63;
	const uint8_t  dst_mode  = (dst >> 3) & 7;
	const uint8_t  dst_reg   = dst & 7;
	const bool     word_mode = !!(instr & 0x8000);
	bool           set_flags = true;

	switch(opcode) {
		case 0b00000011: { // SWAB
					 if (word_mode) // handled elsewhere
						 return false;
					 else {
						 uint16_t v = 0;

						 if (dst_mode == 0) {
							 v = getRegister(dst_reg);

							 v = ((v & 0xff) << 8) | (v >> 8);

							 setRegister(dst_reg, false, v);
						 }
						 else {
							 uint16_t a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
							 v = b->readWord(a);

							 v = ((v & 0xff) << 8) | (v >> 8);

							 set_flags = a != ADDR_PSW;

							 b->writeWord(a, v);
						 }

						 if (set_flags) {
							 setPSW_n(v & 0x80);
							 setPSW_z((v & 0xff) == 0);
							 setPSW_v(false);
							 setPSW_c(false);
						 }
					 }

					 break;
				 }

		case 0b000101000: { // CLR/CLRB
					  if (dst_mode == 0) {
						  uint16_t r = 0;

						  // CLRB only clears the least significant byte
						  if (word_mode)
							  r = getGAM(dst_mode, dst_reg, false, false) & 0xff00;

						  putGAM(dst_mode, dst_reg, false, r, false);
					  }
					  else {
						  uint16_t a = getGAMAddress(dst_mode, dst_reg, word_mode, false);

						  set_flags = a != ADDR_PSW;

						  b -> write(a, word_mode, 0, false);
					  }

					  if (set_flags) {
						  setPSW_n(false);
						  setPSW_z(true);
						  setPSW_v(false);
						  setPSW_c(false);
					  }

					  break;
				  }

		case 0b000101001: { // COM/COMB
					  if (dst_mode == 0) {
						  uint16_t v = getRegister(dst_reg);

						  if (word_mode)
							  v ^= 0xff;
						  else
							  v ^= 0xffff;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(false);
						  setPSW_c(true);

						  setRegister(dst_reg, false, v);
					  }
					  else {
						  uint16_t a = getGAMAddress(dst_mode, dst_reg, word_mode, false);
						  uint16_t v = b -> read(a, word_mode, false);

						  if (word_mode)
							  v ^= 0xff;
						  else
							  v ^= 0xffff;

						  bool set_flags = a != ADDR_PSW;

						  if (set_flags) {
							  setPSW_n(SIGN(v, word_mode));
							  setPSW_z(IS_0(v, word_mode));
							  setPSW_v(false);
							  setPSW_c(true);
						  }

						  b->write(a, word_mode, v, false);
					  }
					  break;
				  }

		case 0b000101010: { // INC/INCB
					  if (dst_mode == 0) {
						  uint16_t v   = getRegister(dst_reg);
						  uint16_t add = word_mode ? v & 0xff00 : 0;

						  v = (v + 1) & (word_mode ? 0xff : 0xffff);
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(word_mode ? (v & 0xff) == 0x80 : v == 0x8000);

						  setRegister(dst_reg, false, v);
					  }
					  else {
						  uint16_t a   = getGAMAddress(dst_mode, dst_reg, word_mode, false);
						  uint16_t v   = b -> read(a, word_mode, false);
						  int32_t  vl  = (v + 1) & (word_mode ? 0xff : 0xffff);

						  bool set_flags = a != ADDR_PSW;

						  if (set_flags) {
							  setPSW_n(SIGN(vl, word_mode));
							  setPSW_z(IS_0(vl, word_mode));
							  setPSW_v(word_mode ? vl == 0x80 : v == 0x8000);
						  }

						  b->write(a, word_mode, vl, false);
					  }

					  break;
				  }

		case 0b000101011: { // DEC/DECB
					  if (dst_mode == 0) {
						  uint16_t v   = getRegister(dst_reg);
						  uint16_t add = word_mode ? v & 0xff00 : 0;

						  v = (v - 1) & (word_mode ? 0xff : 0xffff);
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(word_mode ? (v & 0xff) == 0x7f : v == 0x7fff);

						  setRegister(dst_reg, false, v);
					  }
					  else {
						  uint16_t a   = getGAMAddress(dst_mode, dst_reg, word_mode, false);
						  uint16_t v   = b -> read(a, word_mode, false);
						  int32_t  vl  = (v - 1) & (word_mode ? 0xff : 0xffff);

						  bool set_flags = a != ADDR_PSW;

						  if (set_flags) {
							  setPSW_n(SIGN(vl, word_mode));
							  setPSW_z(IS_0(vl, word_mode));
							  setPSW_v(word_mode ? vl == 0x7f : vl == 0x7fff);
						  }

						  b->write(a, word_mode, vl, false);
					  }

					  break;
				  }

		case 0b000101100: { // NEG/NEGB
					  if (dst_mode == 0) {
						  uint16_t v   = getRegister(dst_reg);
						  uint16_t add = word_mode ? v & 0xff00 : 0;

						  v = (-v) & (word_mode ? 0xff : 0xffff);
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(word_mode ? (v & 0xff) == 0x80 : v == 0x8000);
						  setPSW_c(v);

						  setRegister(dst_reg, false, v);
					  }
					  else {
						  uint16_t a  = getGAMAddress(dst_mode, dst_reg, word_mode, false);
						  uint16_t v  = -b -> read(a, word_mode, false);

						  b->write(a, word_mode, v, false);

						  bool set_flags = a != ADDR_PSW;

						  if (set_flags) {
							  setPSW_n(SIGN(v, word_mode));
							  setPSW_z(IS_0(v, word_mode));
							  setPSW_v(word_mode ? (v & 0xff) == 0x80 : v == 0x8000);
							  setPSW_c(v);
						  }
					  }

					  break;
				  }

		case 0b000101101: { // ADC/ADCB
					  if (dst_mode == 0) {
						  const uint16_t vo    = getRegister(dst_reg);
						  uint16_t       v     = vo;
						  uint16_t       add   = word_mode ? v & 0xff00 : 0;
						  bool           org_c = getPSW_c();

						  v = (v + org_c) & (word_mode ? 0xff : 0xffff);
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v((word_mode ? (vo & 0xff) == 0x7f : vo == 0x7fff) && org_c);
						  setPSW_c((word_mode ? (vo & 0xff) == 0xff : vo == 0xffff) && org_c);

						  setRegister(dst_reg, false, v);
					  }
					  else {
						  uint16_t       a     = getGAMAddress(dst_mode, dst_reg, word_mode, false);
						  const uint16_t vo    = b -> read(a, word_mode, false);
						  bool           org_c = getPSW_c();
						  uint16_t       v     = (vo + org_c) & (word_mode ? 0x00ff : 0xffff);

						  b->write(a, word_mode, v, false);

						  bool set_flags = a != ADDR_PSW;

						  if (set_flags) {
							  setPSW_n(SIGN(v, word_mode));
							  setPSW_z(IS_0(v, word_mode));
							  setPSW_v((word_mode ? (vo & 0xff) == 0x7f : vo == 0x7fff) && org_c);
							  setPSW_c((word_mode ? (vo & 0xff) == 0xff : vo == 0xffff) && org_c);
						  }
					  }

					  break;
				  }

		case 0b000101110: { // SBC/SBCB
					  if (dst_mode == 0) {
						  uint16_t       v     = getRegister(dst_reg);
						  const uint16_t vo    = v;
						  uint16_t       add   = word_mode ? v & 0xff00 : 0;
						  bool           org_c = getPSW_c();

						  v = (v - org_c) & (word_mode ? 0xff : 0xffff);
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(word_mode ? (vo & 0xff) == 0x80 : vo == 0x8000);

						  if (IS_0(vo, word_mode) && org_c)
							  setPSW_c(true);
						  else
							  setPSW_c(false);

						  setRegister(dst_reg, false, v);
					  }
					  else {
						  uint16_t       a     = getGAMAddress(dst_mode, dst_reg, word_mode, false);
						  const uint16_t vo    = b -> read(a, word_mode, false);
						  bool           org_c = getPSW_c();
						  uint16_t       v     = (vo - org_c) & (word_mode ? 0xff : 0xffff);

						  b->write(a, word_mode, v, false);

						  bool set_flags = a != ADDR_PSW;

						  if (set_flags) {
							  setPSW_n(SIGN(v, word_mode));
							  setPSW_z(IS_0(v, word_mode));
							  setPSW_v(word_mode? (vo & 0xff) == 0x80 : v == 0x8000);

							  if (IS_0(vo, word_mode) && org_c)
								  setPSW_c(true);
							  else
								  setPSW_c(false);
						  }
					  }
					  break;
				  }

		case 0b000101111: { // TST/TSTB
					  uint16_t v = getGAM(dst_mode, dst_reg, word_mode, false);

					  setPSW_n(SIGN(v, word_mode));
					  setPSW_z(IS_0(v, word_mode));
					  setPSW_v(false);
					  setPSW_c(false);

					  break;
				  }

		case 0b000110000: { // ROR/RORB
					  if (dst_mode == 0) {
						  uint16_t v         = getRegister(dst_reg);
						  bool     new_carry = v & 1;

						  uint16_t temp = 0;
						  if (word_mode) {
							  uint16_t add = v & 0xff00;

							  temp = (v >> 1) | (getPSW_c() <<  7) | add;
						  }
						  else {
							  temp = (v >> 1) | (getPSW_c() << 15);
						  }

						  setRegister(dst_reg, false, temp);

						  setPSW_c(new_carry);
						  setPSW_n(SIGN(temp, word_mode));
						  setPSW_z(IS_0(temp, word_mode));
						  setPSW_v(getPSW_c() ^ getPSW_n());
					  }
					  else {
						  uint16_t a         = getGAMAddress(dst_mode, dst_reg, word_mode, false);
						  uint16_t t         = b -> read(a, word_mode, false);
						  bool     new_carry = t & 1;

						  uint16_t temp = 0;
						  if (word_mode)
							  temp = (t >> 1) | (getPSW_c() <<  7);
						  else
							  temp = (t >> 1) | (getPSW_c() << 15);

						  b->write(a, word_mode, temp, false);

						  bool set_flags = a != ADDR_PSW;

						  if (set_flags) {
							  setPSW_c(new_carry);
							  setPSW_n(SIGN(temp, word_mode));
							  setPSW_z(IS_0(temp, word_mode));
							  setPSW_v(getPSW_c() ^ getPSW_n());
						  }
					  }
					  break;
				  }

		case 0b000110001: { // ROL/ROLB
					  if (dst_mode == 0) {
						  uint16_t v         = getRegister(dst_reg);
						  bool     new_carry = false;

						  uint16_t temp = 0;
						  if (word_mode) {
							  new_carry = v & 0x80;
							  temp = (((v << 1) | getPSW_c()) & 0xff) | (v & 0xff00);
						  }
						  else {
							  new_carry = v & 0x8000;
							  temp = (v << 1) | getPSW_c();
						  }

						  setRegister(dst_reg, false, temp);

						  setPSW_c(new_carry);
						  setPSW_n(SIGN(temp, word_mode));
						  setPSW_z(IS_0(temp, word_mode));
						  setPSW_v(getPSW_c() ^ getPSW_n());
					  }
					  else {
						  uint16_t a         = getGAMAddress(dst_mode, dst_reg, word_mode, false);
						  uint16_t t         = b -> read(a, word_mode, false);
						  bool     new_carry = false;

						  uint16_t temp = 0;
						  if (word_mode) {
							  new_carry = t & 0x80;
							  temp = ((t << 1) | getPSW_c()) & 0xff;
						  }
						  else {
							  new_carry = t & 0x8000;
							  temp = (t << 1) | getPSW_c();
						  }

						  b->write(a, word_mode, temp, false);

						  bool set_flags = a != ADDR_PSW;

						  if (set_flags) {
							  setPSW_c(new_carry);
							  setPSW_n(SIGN(temp, word_mode));
							  setPSW_z(IS_0(temp, word_mode));
							  setPSW_v(getPSW_c() ^ getPSW_n());
						  }
					  }
					  break;
				  }

		case 0b000110010: { // ASR/ASRB
					  if (dst_mode == 0) {
						  uint16_t v  = getRegister(dst_reg);

						  bool     hb = word_mode ? v & 128 : v & 32768;

						  setPSW_c(v & 1);

						  if (word_mode) {
							  uint16_t add = v & 0xff00;

							  v = (v & 255) >> 1;
							  v |= hb << 7;
							  v |= add;
						  }
						  else {
							  v >>= 1;
							  v |= hb << 15;
						  }

						  setRegister(dst_reg, false, v);

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(getPSW_n() ^ getPSW_c());
					  }
					  else {
						  uint16_t a   = getGAMAddress(dst_mode, dst_reg, word_mode, false);
						  uint16_t v   = b -> read(a, word_mode, false);
						  uint16_t add = word_mode ? v & 0xff00 : 0;

						  bool     hb  = word_mode ? v & 128 : v & 32768;

						  setPSW_c(v & 1);

						  if (word_mode) {
							  v = (v & 255) >> 1;
							  v |= hb << 7;
							  v |= add;
						  }
						  else {
							  v >>= 1;
							  v |= hb << 15;
						  }

						  b->write(a, word_mode, v, false);

						  bool set_flags = a != ADDR_PSW;

						  if (set_flags) {
							  setPSW_n(SIGN(v, word_mode));
							  setPSW_z(IS_0(v, word_mode));
							  setPSW_v(getPSW_n() ^ getPSW_c());
						  }
					  }
					  break;
				  }

		case 0b00110011: { // ASL/ASLB
					 if (dst_mode == 0) {
						 uint16_t vl  = getRegister(dst_reg);
						 uint16_t add = word_mode ? vl & 0xff00 : 0;

						 uint16_t v   = (vl << 1) & (word_mode ? 0xff : 0xffff);
						 v |= add;

						 setPSW_n(SIGN(v, word_mode));
						 setPSW_z(IS_0(v, word_mode));
						 setPSW_c(SIGN(vl, word_mode));
						 setPSW_v(getPSW_n() ^ getPSW_c());

						 setRegister(dst_reg, false, v);
					 }
					 else {
						 uint16_t a   = getGAMAddress(dst_mode, dst_reg, word_mode, false);
						 uint16_t vl  = b -> read(a, word_mode, false);
						 uint16_t v   = (vl << 1) & (word_mode ? 0xff : 0xffff);

						 bool set_flags = a != ADDR_PSW;

						 if (set_flags) {
							 setPSW_n(SIGN(v, word_mode));
							 setPSW_z(IS_0(v, word_mode));
							 setPSW_c(SIGN(vl, word_mode));
							 setPSW_v(getPSW_n() ^ getPSW_c());
						 }

						 b->write(a, word_mode, v, false);
					 }
					 break;
				 }

		case 0b00110101: { // MFPD/MFPI
					 // always words: word_mode-bit is to select between MFPI and MFPD
					 // NOTE: this code does not work for D/I split setups! TODO

					 if ((b->getMMR0() & 0160000) == 0)
						 b->addToMMR1(-2, 6);

					 bool     set_flags = true;
					 uint16_t v         = 0xffff;

					 if (dst_mode == 0)
						 v = getRegister(dst_reg, getBitPSW(11), true);
					 else {
						 // calculate address in current address space
						 uint16_t a = getGAMAddress(dst_mode, dst_reg, false, false);

						 set_flags = a != ADDR_PSW;

						 // read from previous space
						 v = b -> read(a, false, true);
					 }

					 if (set_flags) {
						 setPSW_n(SIGN(v, false));
						 setPSW_z(v == 0);
						 setPSW_v(false);
					 }

					 // put on current stack
					 pushStack(v);

					 break;
				 }

		case 0b00110110: { // MTPI/MTPD
					 // always words: word_mode-bit is to select between MTPI and MTPD
					 // NOTE: this code does not work for D/I split setups! TODO

					 if ((b->getMMR0() & 0160000) == 0)
						 b->addToMMR1(2, 6);

					 // retrieve word from '15/14'-stack
					 uint16_t v = popStack();

					 bool set_flags = true;

					 if (dst_mode == 0)
						setRegister(dst_reg, true, v);
					 else {
						uint16_t a = getGAMAddress(dst_mode, dst_reg, false, false);

						set_flags = a != ADDR_PSW;

						b->write(a, false, v, true);  // put in '13/12' address space
					 }

					 if (set_flags) {
						 setPSW_n(SIGN(v, false));
						 setPSW_z(v == 0);
						 setPSW_v(false);
					 }

					 break;
				 }

		case 0b000110100: // MARK/MTPS (put something in PSW)
				 if (word_mode) {  // MTPS
					 psw &= 0xff00;  // only alter lower 8 bits
					 psw |= getGAM(dst_mode, dst_reg, word_mode, false) & 0xef;  // can't change bit 4
				 }
				 else {
					 setRegister(6, getPC() + dst * 2);

					 setPC(getRegister(5));

					 setRegister(5, popStack());
				 }

				 break;

		case 0b000110111: // MFPS (get PSW to something) / SXT
				 if (word_mode) {  // MFPS
					 uint16_t temp      = psw & 0xff;
					 bool     extend_b7 = psw & 128;

					 if (extend_b7 && dst_mode == 0)
						 temp |= 0xff00;

					 set_flags = putGAM(dst_mode, dst_reg, word_mode, temp, false);

					 if (set_flags) {
						 setPSW_z(temp == 0);
						 setPSW_v(false);
						 setPSW_n(extend_b7);
					 }
				 }
				 else {  // SXT
					 uint16_t a  = getGAMAddress(dst_mode, dst_reg, word_mode, false);

					 int32_t  vl = -getPSW_n();

					 if (put_result(a, dst_mode, dst_reg, word_mode, vl)) {
						 setPSW_z(getPSW_n() == false);
						 setPSW_v(false);
					 }
				 }

				 break;

		default:
				 return false;
	}

	return true;
}

bool cpu::conditional_branch_instructions(const uint16_t instr)
{
	const uint8_t opcode = instr >> 8;
	const int8_t  offset = instr;
	bool          take   = false;

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
			take = getPSW_n() == getPSW_v();
			break;

		case 0b00000101: // BLT
			take = getPSW_n() ^ getPSW_v();
			break;

		case 0b00000110: // BGT
			take = getPSW_n() == getPSW_v() && getPSW_z() == false;
			break;

		case 0b00000111: // BLE
			take = getPSW_n() != getPSW_v() || getPSW_z();
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
			take = getPSW_c() || getPSW_z();
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

//		// trap via vector 010  only(?) on an 11/60 and not(?) on an 11/70
//		trap(010);

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
		DOLOG(debug, true, "stackLimitRegister reached %06o while pushing %06o", stackLimitRegister, v);

		trap(123, 7);  // TODO
	}
	else {
		uint16_t a = addRegister(6, false, -2);

		b -> writeWord(a, v);
	}
}

uint16_t cpu::popStack()
{
	uint16_t a    = getRegister(6);
	uint16_t temp = b -> readWord(a);

	addRegister(6, false, 2);

	return temp;
}

bool cpu::misc_operations(const uint16_t instr)
{
	switch(instr) {
		case 0b0000000000000000: // HALT
			*event = EVENT_HALT;
			return true;

		case 0b0000000000000001: // WAIT
			return true;

		case 0b0000000000000010: {  // RTI
			setPC(popStack());

			uint16_t replacement_psw = popStack();

			setPSW(replacement_psw, true);

			return true;
		}

		case 0b0000000000000011: // BPT
			trap(014);
			return true;

		case 0b0000000000000100: // IOT
			trap(020);
			return true;

		case 0b0000000000000110: {  // RTT
			setPC(popStack());

			uint16_t replacement_psw = popStack();

			setPSW(replacement_psw, true);

			return true;
		}

		case 0b0000000000000111: // MFPT
			if (emulateMFPT)
				setRegister(0, true, 1); // PDP-11/44
			else
				trap(012);
			return true;

		case 0b0000000000000101: // RESET
			b->init();
			init_interrupt_queue();
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
		int dst_mode = (instr >> 3) & 7;

		if (dst_mode == 0)  // cannot jump to a register
			trap(010);
		else {
			int dst_reg = instr & 7;

			bool word_mode = false;
			setPC(getGAMAddress(dst_mode, dst_reg, word_mode, false));
		}

		return true;
	}

	if ((instr & 0b1111111000000000) == 0b0000100000000000) { // JSR
		int      link_reg  = (instr >> 6) & 7;
		uint16_t dst_value = getGAMAddress((instr >> 3) & 7, instr & 7, false, false);

		// PUSH link
		pushStack(getRegister(link_reg));

		// MOVE PC,link
		setRegister(link_reg, false, getPC());

		// JMP dst
		setPC(dst_value);

		return true;
	}

	if ((instr & 0b1111111111111000) == 0b0000000010000000) { // RTS
		const int link_reg = instr & 7;

		uint16_t  v        = popStack();

		// MOVE link, PC
		setPC(getRegister(link_reg));

		// POP link
		setRegister(link_reg, false, v);

		return true;
	}

	return false;
}

void cpu::schedule_trap(const uint16_t vector)
{
	scheduled_trap = vector;
}

// 'is_interrupt' is not correct naming; it is true for mmu faults and interrupts
void cpu::trap(uint16_t vector, const int new_ipl, const bool is_interrupt)
{
	DOLOG(debug, true, "*** CPU::TRAP, MMR0: %06o, MMR2: %06o ***", b->getMMR0(), b->getMMR2());

	int      processing_trap_depth = 0;
	uint16_t before_psw            = 0;
	uint16_t before_pc             = 0;

	for(;;) {
		try {
			processing_trap_depth++;

			bool kernel_mode = psw >> 14;

			if (processing_trap_depth >= 2) {
				DOLOG(debug, true, "Trap depth %d", processing_trap_depth);

				if (kernel_mode)
					vector = 4;

				setRegister(6, 04);

				if (processing_trap_depth >= 3) {
					// TODO: halt?
				}
			}
			else {
				before_psw = getPSW();
				before_pc  = getPC();

				if ((b->getMMR0() & 0160000) == 0 && vector != 4) {
					b->setMMR2(vector);
					b->addToMMR1(-2, 6);
					b->addToMMR1(-2, 6);
				}

				if (is_interrupt)
					b->clearMMR0Bit(12);
				else
					b->setMMR0Bit(12);  // it's a trap
			}

			// make sure the trap vector is retrieved from kernel space
			psw &= 037777;  // mask off 14/15

			setPC(b->readWord(vector + 0));

			// switch to kernel mode & update 'previous mode'
			uint16_t new_psw = b->readWord(vector + 2) & 0147777;  // mask off old 'previous mode'
			if (new_ipl != -1)
				new_psw = (new_psw & ~0xe0) | (new_ipl << 5);
			new_psw |= (before_psw >> 2) & 030000; // apply new 'previous mode'
			setPSW(new_psw, false);

		//	DOLOG(info, true, "R6: %06o, before PSW: %06o, new PSW: %06o", getRegister(6), before_psw, new_psw);
		//
			if (processing_trap_depth >= 2 && kernel_mode)
				setRegister(6, 04);

			pushStack(before_psw);
			pushStack(before_pc);

			// if we reach this point then the trap was processed without causing
			// another trap
			break;
		}
		catch(const int exception) {
			DOLOG(debug, true, "trap during execution of trap (%d)", exception);

			setPSW(before_psw, false);
		}
	}

	setPC(b->readWord(vector + 0, d_space));

	if (!is_interrupt)
		b->setMMR0Bit(12);  // it's a trap

	// switch to kernel mode & update 'previous mode'
	uint16_t new_psw = b->readWord(vector + 2, d_space) & 0147777;  // mask off old 'previous mode'
	if (new_ipl != -1)
		new_psw = (new_psw & ~0xe0) | (new_ipl << 5);
	new_psw |= (before_psw >> 2) & 030000; // apply new 'previous mode'
	setPSW(new_psw, false);

	pushStack(before_psw);
	pushStack(before_pc);
}

cpu::operand_parameters cpu::addressing_to_string(const uint8_t mode_register, const uint16_t pc, const bool word_mode) const
{
	assert(mode_register < 64);

	uint16_t    next_word = b->peekWord(pc & 65535);

	int         reg       = mode_register & 7;

	uint16_t    mask      = word_mode ? 0xff : 0xffff;

	std::string reg_name;
	if (reg == 6)
		reg_name = "SP";
	else if (reg == 7)
		reg_name = "PC";
	else
		reg_name = format("R%d", reg);

	switch(mode_register >> 3) {
		case 0:
			return { reg_name, 2, -1, uint16_t(getRegister(reg) & mask) };

		case 1:
			return { format("(%s)", reg_name.c_str()), 2, -1, uint16_t(b->peekWord(getRegister(reg)) & mask) };

		case 2:
			if (reg == 7)
				return { format("#%06o", next_word), 4, int(next_word), uint16_t(next_word & mask) };

			return { format("(%s)+", reg_name.c_str()), 2, -1, uint16_t(b->peekWord(getRegister(reg)) & mask) };

		case 3:
			if (reg == 7)
				return { format("@#%06o", next_word), 4, int(next_word), uint16_t(b->peekWord(next_word) & mask) };

			return { format("@(%s)+", reg_name.c_str()), 2, -1, uint16_t(b->peekWord(b->peekWord(getRegister(reg))) & mask) };

		case 4:
			return { format("-(%s)", reg_name.c_str()), 2, -1, uint16_t(b->peekWord(getRegister(reg) - (word_mode == false || reg >= 6 ? 2 : 1)) & mask) };

		case 5:
			return { format("@-(%s)", reg_name.c_str()), 2, -1, uint16_t(b->peekWord(b->peekWord(getRegister(reg) - 2)) & mask) };

		case 6:
			if (reg == 7)
				return { format("%06o", (pc + next_word + 2) & 65535), 4, int(next_word), uint16_t(b->peekWord(getRegister(reg) + next_word) & mask) };

			return { format("%o(%s)", next_word, reg_name.c_str()), 4, int(next_word), uint16_t(b->peekWord(getRegister(reg) + next_word) & mask) };

		case 7:
			if (reg == 7)
				return { format("@%06o", next_word), 4, int(next_word), uint16_t(b->peekWord(b->peekWord(getRegister(reg) + next_word)) & mask) };

			return { format("@%o(%s)", next_word, reg_name.c_str()), 4, int(next_word), uint16_t(b->peekWord(b->peekWord(getRegister(reg) + next_word)) & mask) };
	}

	return { "??", 0, -1, 0123456 };
}

std::map<std::string, std::vector<std::string> > cpu::disassemble(const uint16_t addr) const
{
	uint16_t    instruction   = b->peekWord(addr);

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

	std::vector<uint16_t> instruction_words { instruction };
	std::vector<uint16_t> work_values;

	// TODO: 100000011

	if (do_opcode == 0b000) {
		auto dst_text { addressing_to_string(dst_register, (addr + 2) & 65535, word_mode) };

		auto next_word = dst_text.instruction_part;

		work_values.push_back(dst_text.work_value);

		// single_operand_instructions
		switch(so_opcode) {
			case 0b00000011:
				if (!word_mode)
					text = "SWAB " + dst_text.operand;
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
				if (word_mode == true)
					name = "MTPS";
				break;

			case 0b000110111:
				if (word_mode == true)
					name = "MFPS";
				else
					name = "SXT";
				break;
		}

		if (text.empty() && name.empty() == false)
			text = name + word_mode_str + space + dst_text.operand;

		if (text.empty() == false && next_word != -1)
			instruction_words.push_back(next_word);
	}
	else if (do_opcode == 0b111) {
		std::string src_text = format("R%d", (instruction >> 6) & 7);
		auto        dst_text { addressing_to_string(dst_register, (addr + 2) & 65535, word_mode) };

		auto next_word = dst_text.instruction_part;

		work_values.push_back(dst_text.work_value);

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
			text = name + space + src_text + comma + dst_text.operand;

		if (text.empty() == false && next_word != -1)
			instruction_words.push_back(next_word);
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

		// source
		auto src_text { addressing_to_string(src_register, (addr + 2) & 65535, word_mode) };

		auto next_word_src = src_text.instruction_part;
		if (next_word_src != -1)
			instruction_words.push_back(next_word_src);

		work_values.push_back(src_text.work_value);

		// destination
		auto dst_text { addressing_to_string(dst_register, (addr + src_text.length) & 65535, word_mode) };

		auto next_word_dst = dst_text.instruction_part;
		if (next_word_dst != -1)
			instruction_words.push_back(next_word_dst);

		work_values.push_back(dst_text.work_value);

		text = name + word_mode_str + space + src_text.operand + comma + dst_text.operand;
	}

	if (text.empty()) {  // conditional branch instructions
		uint8_t  cb_opcode = (instruction >> 8) & 255;
		int8_t   offset    = instruction & 255;
		uint16_t new_pc    = (addr + 2 + offset * 2) & 65535;

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
				work_values.clear();
				break;

			case 0b0000000000000000:
				text = "HALT";
				work_values.clear();
				break;

			case 0b0000000000000001:
				text = "WAIT";
				work_values.clear();
				break;

			case 0b0000000000000010:
				text = "RTI";
				work_values.clear();
				break;

			case 0b0000000000000011:
				text = "BPT";
				break;

			case 0b0000000000000100:
				text = "IOT";
				break;

			case 0b0000000000000110:
				text = "RTT";
				work_values.clear();
				break;

			case 0b0000000000000111:
				text = "MFPT";
				break;

			case 0b0000000000000101:
				text = "RESET";
				work_values.clear();
				break;
		}

		if ((instruction >> 8) == 0b10001000)
			text = format("EMT %o", instruction & 255);

		if ((instruction >> 8) == 0b10001001)
			text = format("TRAP %o", instruction & 255);

		if ((instruction & ~0b111111) == 0b0000000001000000) {
			auto dst_text { addressing_to_string(dst_register, (addr + 2) & 65535, word_mode) };

			auto next_word = dst_text.instruction_part;
			if (next_word != -1)
				instruction_words.push_back(next_word);

			work_values.push_back(dst_text.work_value);

			text = std::string("JMP ") + dst_text.operand;
		}

		if ((instruction & 0b1111111000000000) == 0b0000100000000000) {
			auto dst_text { addressing_to_string(dst_register, (addr + 2) & 65535, word_mode) };

			auto next_word = dst_text.instruction_part;
			if (next_word != -1)
				instruction_words.push_back(next_word);

			work_values.push_back(dst_text.work_value);

			text = format("JSR R%d,", src_register & 7) + dst_text.operand;
		}

		if ((instruction & 0b1111111111111000) == 0b0000000010000000)
			text = "RTS";
	}

	if (text.empty())
		text = "???";

	std::map<std::string, std::vector<std::string> > out;

	// MOV x,y
	out.insert({ "address", { format("%06o", addr) } });

	// MOV x,y
	out.insert({ "instruction-text", { text } });

	// words making up the instruction
	std::vector<std::string> instruction_values;
	for(auto i : instruction_words)
		instruction_values.push_back(format("%06o", i));

	out.insert({ "instruction-values", instruction_values });

	// R0-R5, SP, PC
	std::vector<std::string> registers;

	for(int i=0; i<8; i++) {
		if (i < 6)
			registers.push_back(format("%06o", getRegister(i)));
		else if (i == 6)
			registers.push_back(format("%06o", sp[psw >> 14]));
		else
			registers.push_back(format("%06o", addr));
	}

	out.insert({ "registers", registers });

	// PSW
	std::string psw_str = format("%d%d|%d|%d|%c%c%c%c%c", psw >> 14, (psw >> 12) & 3, (psw >> 11) & 1, (psw >> 5) & 7,
                        psw & 16?'t':'-', psw & 8?'n':'-', psw & 4?'z':'-', psw & 2 ? 'v':'-', psw & 1 ? 'c':'-');
	out.insert({ "psw", { psw_str } });

	// values worked with
	std::vector<std::string> work_values_str;
	for(auto v : work_values)
		work_values_str.push_back(format("%06o", v));
	out.insert({ "work-values", work_values_str });

	out.insert({ "MMR0", { format("%06o", b->getMMR0()) } });
	out.insert({ "MMR2", { format("%06o", b->getMMR2()) } });

	return out;
}

void cpu::step_a()
{
	if ((b->getMMR0() & 0160000) == 0)
		b->clearMMR1();

	if (scheduled_trap) {
		trap(scheduled_trap, 7, true);

		scheduled_trap = 0;

		return;
	}

	if (check_queued_interrupts())
	       return;
}

void cpu::step_b()
{
	instruction_count++;

	uint16_t temp_pc = getPC();

	if ((b->getMMR0() & 0160000) == 0)
		b->setMMR2(temp_pc);

	try {
		uint16_t instr = b->readWord(temp_pc);

//		FILE *fh = fopen("/home/folkert/kek.dat", "a+");
//		fprintf(fh, "%06o %06o\n", temp_pc, instr);
//		fclose(fh);

		addRegister(7, false, 2);

		if (double_operand_instructions(instr))
			return;

		if (conditional_branch_instructions(instr))
			return;

		if (condition_code_operations(instr))
			return;

		if (misc_operations(instr))
			return;

		DOLOG(warning, true, "UNHANDLED instruction %o", instr);

		trap(010);
	}
	catch(const int exception) {
		DOLOG(debug, true, "bus-trap during execution of command (%d)", exception);
	}
}
