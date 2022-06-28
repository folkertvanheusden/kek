// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "gen.h"
#include "cpu.h"
#include "log.h"
#include "memory.h"
#include "tm-11.h"
#include "tty.h"

#if defined(ESP32)
// ESP32 goes in a crash-loop when allocating 128kB
// see also https://github.com/espressif/esp-idf/issues/1934
constexpr int n_pages = 12;
#else
constexpr int n_pages = 16;
#endif


bus::bus()
{
	m = new memory(n_pages * 8192);

	memset(pages, 0x00, sizeof pages);

	CPUERR = MMR0 = MMR1 = MMR2 = MMR3 = PIR = CSR = 0;
}

bus::~bus()
{
	delete c;
	delete tm11;
	delete rk05_;
	delete rl02_;
	delete tty_;
	delete m;
}

void bus::clearmem()
{
	m -> reset();
}

void bus::init()
{
	MMR0 = 0;
	MMR3 = 0;
}

uint16_t bus::read_pdr(const uint32_t a, const int run_mode, const bool word_mode, const bool peek_only)
{
	bool is_11_34 = c->get_34();
	int      page = (a >> 1) & 7;
	bool     is_d = is_11_34 ? false : (a & 16);
	uint16_t t    = pages[run_mode][is_d][page].pdr;

	DOLOG(debug, !peek_only, "read run-mode %d: %c PDR for %d: %o", run_mode, is_d ? 'D' : 'I', page, t);

	return word_mode ? (a & 1 ? t >> 8 : t & 255) : t;
}

uint16_t bus::read_par(const uint32_t a, const int run_mode, const bool word_mode, const bool peek_only)
{
	bool is_11_34 = c->get_34();
	int      page = (a >> 1) & 7;
	bool     is_d = is_11_34 ? false : (a & 16);
	uint16_t t    = pages[run_mode][is_d][page].par;

	DOLOG(debug, !peek_only, "read run-mode %d: %c PAR for %d: %o (phys: %07o)", run_mode, is_d ? 'D' : 'I', page, t, t * 64);

	return word_mode ? (a & 1 ? t >> 8 : t & 255) : t;
}

uint16_t bus::read(const uint16_t a, const bool word_mode, const bool use_prev, const bool peek_only)
{
	uint16_t temp = 0;

	if (a >= 0160000) {
		if (word_mode)
			DOLOG(debug, false, "READ I/O %06o in byte mode", a);

		if (a == 0177750) { // MAINT
			DOLOG(debug, !peek_only, "read MAINT");
			return 1; // POWER OK
		}

		if (a == 0177570) { // console switch & display register
			DOLOG(debug, !peek_only, "read console switch (%06o)", console_switches);

			return console_switches;
		}

		if (a == 0172540) { // KW11P programmable clock
			DOLOG(debug, !peek_only, "read programmable clock");
			return 128;
		}

		if (a == 0177772) { // PIR
			DOLOG(debug, !peek_only, "read PIT");
			return PIR;
		}

		if (a == 0177546) { // line frequency clock and status register
			DOLOG(debug, !peek_only, "read line freq clock");
			return lf_csr;
		}

		if (a == 0177514) { // printer, CSR register, LP11
			DOLOG(debug, !peek_only, "read LP11 CSR");
			return 0x80;
		}

		/// MMU ///
		if (a >= 0172200 && a < 0172240)
			return read_pdr(a, 1, word_mode, peek_only);
		else if (a >= 0172240 && a < 0172300)
			return read_par(a, 1, word_mode, peek_only);
		else if (a >= 0172300 && a < 0172340)
			return read_pdr(a, 0, word_mode, peek_only);
		else if (a >= 0172340 && a < 0172400)
			return read_par(a, 0, word_mode, peek_only);
		else if (a >= 0177600 && a < 0177640)
			return read_pdr(a, 3, word_mode, peek_only);
		else if (a >= 0177640 && a < 0177700)
			return read_par(a, 3, word_mode, peek_only);
		///////////

		if (word_mode) {
			if (a == 0177776) { // PSW
				DOLOG(debug, !peek_only, "readb PSW LSB");
				return c -> getPSW() & 255;
			}

			if (a == 0177777) {
				DOLOG(debug, !peek_only, "readb PSW MSB");
				return c -> getPSW() >> 8;
			}

			if (a == 0177774) { // stack limit register
				DOLOG(debug, !peek_only, "readb stack limit register");
				return c -> getStackLimitRegister() & 0xff;
			}
			if (a == 0177775) { // stack limit register
				DOLOG(debug, !peek_only, "readb stack limit register");
				return c -> getStackLimitRegister() >> 8;
			}

			if (a >= 0177700 && a <= 0177705) { // kernel R0-R5
				DOLOG(debug, !peek_only, "readb kernel R%d", a - 0177700);
				return c -> getRegister(a - 0177700, 0, false) & 0xff;
			}
			if (a >= 0177710 && a <= 0177715) { // user R0-R5
				DOLOG(debug, !peek_only, "readb user R%d", a - 0177710);
				return c -> getRegister(a - 0177710, 3, false) & 0xff;
			}
			if (a == 0177706) { // kernel SP
				DOLOG(debug, !peek_only, "readb kernel sp");
				return c -> getStackPointer(0) & 0xff;
			}
			if (a == 0177707) { // PC
				DOLOG(debug, !peek_only, "readb pc");
				return c -> getPC() & 0xff;
			}
			if (a == 0177716) { // supervisor SP
				DOLOG(debug, !peek_only, "readb supervisor sp");
				return c -> getStackPointer(1) & 0xff;
			}
			if (a == 0177717) { // user SP
				DOLOG(debug, !peek_only, "readb user sp");
				return c -> getStackPointer(3) & 0xff;
			}

			if (a == 0177766) { // cpu error register
				DOLOG(debug, !peek_only, "readb cpuerr");
				return CPUERR & 0xff;
			}
		}
		else {
			if (a == 0177572) {
				DOLOG(debug, !peek_only, "read MMR0");
				return MMR0;
			}

			if (a == 0177574) { // MMR1
				DOLOG(debug, !peek_only, "read MMR1");
				return MMR1;
			}

			if (a == 0177576) { // MMR2
				DOLOG(debug, !peek_only, "read MMR2");
				return MMR2;
			}

			if (a == 0172516) { // MMR3
				DOLOG(debug, !peek_only, "read MMR3");
				return MMR3;
			}

			if (a == 0177776) { // PSW
				DOLOG(debug, !peek_only, "read PSW");
				return c -> getPSW();
			}

			if (a == 0177774) { // stack limit register
				return c -> getStackLimitRegister();
			}

			if (a >= 0177700 && a <= 0177705) { // kernel R0-R5
				DOLOG(debug, !peek_only, "read kernel R%d", a - 0177700);
				return c -> getRegister(a - 0177700, 0, false);
			}
			if (a >= 0177710 && a <= 0177715) { // user R0-R5
				DOLOG(debug, !peek_only, "read user R%d", a - 0177710);
				return c -> getRegister(a - 0177710, 3, false);
			}
			if (a == 0177706) { // kernel SP
				DOLOG(debug, !peek_only, "read kernel sp");
				return c -> getStackPointer(0);
			}
			if (a == 0177707) { // PC
				DOLOG(debug, !peek_only, "read pc");
				return c -> getPC();
			}
			if (a == 0177716) { // supervisor SP
				DOLOG(debug, !peek_only, "read supervisor sp");
				return c -> getStackPointer(1);
			}
			if (a == 0177717) { // user SP
				DOLOG(debug, !peek_only, "read user sp");
				return c -> getStackPointer(3);
			}

			if (a == 0177766) { // cpu error register
				DOLOG(debug, !peek_only, "read CPUERR");
				return CPUERR;
			}
		}

		if (tm11 && a >= TM_11_BASE && a < TM_11_END)
			return word_mode ? tm11 -> readByte(a) : tm11 -> readWord(a);

		if (rk05_ && a >= RK05_BASE && a < RK05_END)
			return word_mode ? rk05_ -> readByte(a) : rk05_ -> readWord(a);

		if (rl02_ && a >= RL02_BASE && a < RL02_END)
			return word_mode ? rl02_ -> readByte(a) : rl02_ -> readWord(a);

		if (tty_ && a >= PDP11TTY_BASE && a < PDP11TTY_END) {
			if (peek_only)
				return 012345;

			return word_mode ? tty_ -> readByte(a) : tty_ -> readWord(a);
		}

		// LO size register field must be all 1s, so subtract 1
		constexpr uint32_t system_size = n_pages * 8192 / 64 - 1;

		if (a == 0177762)  // system size HI
			return system_size >> 16;

		if (a == 0177760)  // system size LO
			return system_size & 65535;

		if (a & 1)
			DOLOG(debug, !peek_only, "bus::readWord: odd address UNHANDLED %o", a);

		DOLOG(debug, !peek_only, "UNHANDLED read %o(%c)", a, word_mode ? 'B' : ' ');

		return -1;
	}

	int      run_mode = (c->getPSW() >> (use_prev ? 12 : 14)) & 3;

	uint32_t m_offset = calculate_physical_address(run_mode, a, !peek_only, false, peek_only, word_mode);

	if (word_mode)
		temp = m -> readByte(m_offset);
	else
		temp = m -> readWord(m_offset);

	DOLOG(debug, !peek_only, "READ from %06o/%07o: %o", a, m_offset, temp);

	return temp;
}

void bus::setMMR0(int value)
{
	value &= ~(3 << 10);  // bit 10 & 11 always read as 0

	if (value & 1)
		value &= ~(7 << 13);  // reset error bits

	if (MMR0 & 0160000) {
		if ((value & 1) == 0)
			value &= 254;  // bits 7...1 are protected 
	}

// TODO if bit 15/14/13 are set (either of them), then do not modify bit 1...7

	MMR0 = value;
}

void bus::setMMR0Bit(const int bit)
{
	assert(bit != 10 && bit != 11);
	assert(bit < 16 && bit >= 0);

	MMR0 |= 1 << bit;
}

void bus::clearMMR0Bit(const int bit)
{
	assert(bit != 10 && bit != 11);
	assert(bit < 16 && bit >= 0);

	MMR0 &= ~(1 << bit);
}

void bus::setMMR2(const uint16_t value) 
{
	MMR2 = value;
}

uint32_t bus::calculate_physical_address(const int run_mode, const uint16_t a, const bool trap_on_failure, const bool is_write, const bool peek_only, const bool word_mode)
{
	uint32_t m_offset = a;

	const uint8_t apf = a >> 13; // active page field

	if ((a & 1) && word_mode == 0 && peek_only == false) {
		DOLOG(debug, true, "TRAP(004) (throw 5) on address %06o, page %d, run mode %d, MMR0 %06o, MMR2 %06o", a, apf, run_mode, MMR0, MMR2);

		if (is_write)
			pages[run_mode][0][apf].pdr |= 1 << 7;  // TODO: D/I

		//MMR0 &= ~14;  // add current page
		//MMR0 |= apf << 1;

		c->schedule_trap(004);  // invalid access

		throw 5;
	}

	if (MMR0 & 1) {
		// TODO: D/I
		m_offset = pages[run_mode][0][apf].par * 64;  // memory offset  TODO: handle 16b int-s

		uint16_t p_offset = a & 8191;  // page offset

		m_offset += p_offset;

		if ((MMR3 & 16) == 0)  // off is 18bit
			m_offset &= 0x3ffff;

		if (trap_on_failure) {
			if ((MMR0 & (1 << 9)) || c->get_34()) {
				const int access_control = pages[run_mode][0][apf].pdr & 7;

				if (is_write && access_control != 6) {  // write
					DOLOG(debug, true, "TRAP(0250) (throw 1) for access_control %d on address %06o", access_control, a);

					c->schedule_trap(0250);  // invalid address

					if (is_write)
						pages[run_mode][0][apf].pdr |= 1 << 7;  // TODO: D/I

					if ((MMR0 & 0160000) == 0) {
						MMR0 &= 017777;
						MMR0 |= 1 << 13;  // read-only

						MMR0 &= ~(3 << 5);
						MMR0 |= run_mode << 5;  // TODO: kernel-mode or user-mode when a trap occurs in user-mode?

						MMR0 &= ~14;  // add current page
						MMR0 |= apf << 1;
					}

					DOLOG(debug, true, "MMR0: %06o", MMR0);

					throw 1;
				}
				else if (!is_write) { // read
					if (access_control == 0 || access_control == 1 || access_control == 3 || access_control == 4 || access_control == 7) {
						DOLOG(debug, true, "TRAP(4) (throw 2) for access_control %d on address %06o", access_control, a);

						c->schedule_trap(0250);  // invalid address

						if (is_write)
							pages[run_mode][0][apf].pdr |= 1 << 7;  // TODO: D/I

						if ((MMR0 & 0160000) == 0) {
							MMR0 &= 017777;

							if (access_control == 0 || access_control == 4)
								MMR0 |= 1 << 15;  // not resident
							else
								MMR0 |= 1 << 13;  // read-only

							MMR0 &= ~(3 << 5);
							MMR0 |= run_mode << 5;

							MMR0 &= ~14;  // add current page
							MMR0 |= apf << 1;
						}

						throw 2;
					}
				}
			}

			if (m_offset >= n_pages * 8192) {
				DOLOG(debug, !peek_only, "bus::calculate_physical_address %o >= %o", m_offset, n_pages * 8192);
				DOLOG(debug, true, "TRAP(04) (throw 3) on address %06o", a);

				if ((MMR0 & 0160000) == 0) {
					MMR0 &= 017777;
					MMR0 |= 1 << 15;  // non-resident

					MMR0 &= ~14;  // add current page
					MMR0 |= apf << 1;

					MMR0 &= ~(3 << 5);
					MMR0 |= run_mode << 5;
				}

				if (is_write)
					pages[run_mode][0][apf].pdr |= 1 << 7;  // TODO: D/I

				c->schedule_trap(04);

				throw 3;
			}

			// uint16_t pdr_len = ((pages[run_mode][0][apf].pdr >> 8) & 127) * 64;  // TODO: D/I
			uint16_t pdr_len = (pages[run_mode][0][apf].pdr >> 8) & 127;
			uint16_t pdr_cmp = (a >> 6) & 127;

			bool direction = pages[run_mode][0][apf].pdr & 8;  // TODO: D/I

			// DOLOG(debug, true, "p_offset %06o pdr_len %06o direction %d, run_mode %d, apf %d, pdr: %06o", p_offset, pdr_len, direction, run_mode, apf, pages[run_mode][0][apf].pdr);

			if ((pdr_cmp > pdr_len && direction == false) || (pdr_cmp < pdr_len && direction == true)) {
				DOLOG(debug, !peek_only, "bus::calculate_physical_address::p_offset %o versus %o direction %d", pdr_cmp, pdr_len, direction);
				DOLOG(debug, true, "TRAP(0250) (throw 4) on address %06o", a);
				c->schedule_trap(0250);  // invalid access

				if ((MMR0 & 0160000) == 0) {
					MMR0 &= 017777;
					MMR0 |= 1 << 14;  // length

					MMR0 &= ~14;  // add current page
					MMR0 |= apf << 1;

					MMR0 &= ~(3 << 5);
					MMR0 |= run_mode << 5;
				}

				if (is_write)
					pages[run_mode][0][apf].pdr |= 1 << 7;  // TODO: D/I

				throw 4;
			}
		}

		DOLOG(debug, !peek_only, "virtual address %06o maps to physical address %08o (run_mode: %d, apf: %d, par: %08o, poff: %o, AC: %d)", a, m_offset, run_mode, apf, pages[run_mode][0][apf].par * 64, p_offset, pages[run_mode][0][apf].pdr & 7);  // TODO: D/I
	}

	return m_offset;
}

void bus::clearMMR1()
{
	MMR1 = 0;
}

void bus::addToMMR1(const int8_t delta, const uint8_t reg)
{
	MMR1 <<= 8;

	MMR1 |= (delta & 31) << 3;
	MMR1 |= reg;
}

void bus::write_pdr(const uint32_t a, const int run_mode, const uint16_t value, const bool word_mode)
{
	bool is_11_34 = c->get_34();
	bool is_d = is_11_34 ? false : (a & 16);
	int  page = (a >> 1) & 7;

	if (word_mode) {
		a & 1 ? (pages[run_mode][is_d][page].pdr &= 0xff,   pages[run_mode][is_d][page].pdr |= value << 8) :
			(pages[run_mode][is_d][page].pdr &= 0xff00, pages[run_mode][is_d][page].pdr |= value);
	}
	else {
		pages[run_mode][is_d][page].pdr = value;
	}

	if (is_11_34)  // 11/34 has no cache bit
		pages[run_mode][is_d][page].pdr &= 077416;
	else
		pages[run_mode][is_d][page].pdr &= ~(128 + 64 + 32 + 16);  // set bit 4 & 5 to 0 as they are unused and A/W are set to 0 by writes

	DOLOG(debug, true, "write run-mode %d: %c PDR for %d: %o [%d]", run_mode, is_d ? 'D' : 'I', page, value, word_mode);
}

void bus::write_par(const uint32_t a, const int run_mode, const uint16_t value, const bool word_mode)
{
	bool is_11_34 = c->get_34();
	bool is_d = is_11_34 ? false : (a & 16);
	int  page = (a >> 1) & 7;

	if (word_mode) {
		a & 1 ? (pages[run_mode][is_d][page].par &= 0xff,   pages[run_mode][is_d][page].par |= value << 8) :
			(pages[run_mode][is_d][page].par &= 0xff00, pages[run_mode][is_d][page].par |= value);
	}
	else {
		pages[run_mode][is_d][page].par = value;
	}

	if (is_11_34)  // 11/34 has 12 bit PARs
		pages[run_mode][is_d][page].par &= 4095;

	DOLOG(debug, true, "write run-mode %d: %c PAR for %d: %o (%07o)", run_mode, is_d ? 'D' : 'I', page, word_mode ? value & 0xff : value, pages[run_mode][is_d][page].par * 64);
}

void bus::write(const uint16_t a, const bool word_mode, uint16_t value, const bool use_prev)
{
	int  run_mode = (c->getPSW() >> (use_prev ? 12 : 14)) & 3;

	if ((MMR0 & 1) == 1 && (a & 1) == 0 && a != 0177572) {
		const uint8_t apf = a >> 13; // active page field

                // TODO: D/I
                pages[run_mode][0][apf].pdr |= 64;  // set 'W' (written to) bit
	}

	if (a >= 0160000) {
		if (word_mode) {
			assert(value < 256);
			DOLOG(debug, true, "WRITE I/O %06o in byte mode", a);
		}

		if (word_mode) {
			if (a == 0177776 || a == 0177777) { // PSW
				DOLOG(debug, true, "writeb PSW %s", a & 1 ? "MSB" : "LSB");
				uint16_t vtemp = c -> getPSW();

				if (a & 1)
					vtemp = (vtemp & 0x00ff) | (value << 8);
				else
					vtemp = (vtemp & 0xff00) | value;

				vtemp &= ~16;  // cannot set T bit via this

				c -> setPSW(vtemp, false);

				return;
			}

			if (a == 0177774 || a == 0177775) { // stack limit register
				DOLOG(debug, true, "writeb Set stack limit register: %o", value);
				uint16_t v = c -> getStackLimitRegister();

				if (a & 1)
					v = (v & 0x00ff) | (value << 8);
				else
					v = (v & 0xff00) | value;

				c -> setStackLimitRegister(v);
				return;
			}
		}
		else {
			if (a == 0177776) { // PSW
				DOLOG(debug, true, "write PSW %o", value);
				c -> setPSW(value & ~16, false);
				return;
			}

			if (a == 0177774) { // stack limit register
				DOLOG(debug, true, "write Set stack limit register: %o", value);
				c -> setStackLimitRegister(value);
				return;
			}

			if (a >= 0177700 && a <= 0177705) { // kernel R0-R5
				DOLOG(debug, true, "write kernel R%d: %o", a - 01777700, value);
				c -> setRegister(a - 0177700, false, false, value);
				return;
			}
			if (a >= 0177710 && a <= 0177715) { // user R0-R5
				DOLOG(debug, true, "write user R%d: %o", a - 01777710, value);
				c -> setRegister(a - 0177710, true, false, value);
				return;
			}
			if (a == 0177706) { // kernel SP
				DOLOG(debug, true, "write kernel SP: %o", value);
				c -> setStackPointer(0, value);
				return;
			}
			if (a == 0177707) { // PC
				DOLOG(debug, true, "write PC: %o", value);
				c -> setPC(value);
				return;
			}
			if (a == 0177716) { // supervisor SP
				DOLOG(debug, true, "write supervisor sp: %o", value);
				c -> setStackPointer(1, value);
				return;
			}
			if (a == 0177717) { // user SP
				DOLOG(debug, true, "write user sp: %o", value);
				c -> setStackPointer(3, value);
				return;
			}

			if (a == 0177770) {  // microprogram break register
				return;
			}
		}

		if (a == 0177766) { // cpu error register
			DOLOG(debug, true, "write CPUERR: %o", value);
			CPUERR = 0;
			return;
		}

		if (a == 0172516) { // MMR3
			DOLOG(debug, true, "write set MMR3: %o", value);
			MMR3 = value & 067;
			return;
		}

		if (a == 0177572) { // MMR0
			DOLOG(debug, true, "write set MMR0: %o", value);

			setMMR0(value);

			return;
		}

		if (a == 0177772) { // PIR
			DOLOG(debug, true, "write set PIR: %o", value);
			PIR = value; // TODO
			return;
		}

		if (a == 0177546) { // line frequency clock and status register
			DOLOG(debug, true, "write set LFC/SR: %o", value);
			lf_csr = value;
			return;
		}

		if (tm11 && a >= TM_11_BASE && a < TM_11_END) {
			word_mode ? tm11 -> writeByte(a, value) : tm11 -> writeWord(a, value);
			return;
		}

		if (rk05_ && a >= RK05_BASE && a < RK05_END) {
			word_mode ? rk05_ -> writeByte(a, value) : rk05_ -> writeWord(a, value);
			return;
		}

		if (rl02_ && a >= RL02_BASE && a < RL02_END) {
			word_mode ? rl02_ -> writeByte(a, value) : rl02_ -> writeWord(a, value);
			return;
		}

		if (tty_ && a >= PDP11TTY_BASE && a < PDP11TTY_END) {
			word_mode ? tty_ -> writeByte(a, value) : tty_ -> writeWord(a, value);
			return;
		}

		/// MMU ///
		// supervisor
		if (a >= 0172200 && a < 0172240) {
			write_pdr(a, 1, value, word_mode);
			return;
		}
		if (a >= 0172240 && a < 0172300) {
			write_par(a, 1, value, word_mode);
			return;
		}

		// kernel
		if (a >= 0172300 && a < 0172340) {
			write_pdr(a, 0, value, word_mode);
			return;
		}
		if (a >= 0172340 && a < 0172400) {
			write_par(a, 0, value, word_mode);
			return;
		}

		// user
		if (a >= 0177600 && a < 0177640) {
			write_pdr(a, 3, value, word_mode);
			return;
		}
		if (a >= 0177640 && a < 0177700) {
			write_par(a, 3, value, word_mode);
			return;
		}
		////

		if (a == 0177746) { // cache control register
			// TODO
			return;
		}

		if (a == 0177570) {  // switch register
			switch_register = value;
			return;
		}

		///////////

		if (a == 0177374) { // TODO
			DOLOG(debug, true, "char: %c", value & 127);
			return;
		}

		if (a & 1)
			DOLOG(debug, true, "bus::writeWord: odd address UNHANDLED");

		DOLOG(debug, true, "UNHANDLED write %o(%c): %o", a, word_mode ? 'B' : ' ', value);

//		c -> busError();

		return;
	}

	uint32_t m_offset = calculate_physical_address(run_mode, a, true, true, false, word_mode);

	DOLOG(debug, true, "WRITE to %06o/%07o: %o", a, m_offset, value);

	if (word_mode)
		m->writeByte(m_offset, value);
	else
		m->writeWord(m_offset, value);
}

uint16_t bus::readWord(const uint16_t a)
{
	return read(a, false, false, false);
}

uint16_t bus::peekWord(const uint16_t a)
{
	return read(a, false, false, true);
}

void bus::writeWord(const uint16_t a, const uint16_t value)
{
	write(a, false, value, false);
}

uint16_t bus::readUnibusByte(const uint16_t a)
{
	return m->readByte(a);
}

void bus::writeUnibusByte(const uint16_t a, const uint8_t v)
{
	m->writeByte(a, v);
}

void bus::set_lf_crs_b7()
{
	lf_csr |= 128;
}

uint8_t bus::get_lf_crs()
{
	return lf_csr;
}
