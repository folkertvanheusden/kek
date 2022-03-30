// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "gen.h"
#include "cpu.h"
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

	for(int rm=0; rm<4; rm++) {
		for(int i=0; i<16; i++) {
			pages[rm][i].par = (i & 7) * 8192 / 64;
			pages[rm][i].pdr = (3 << 1) | (0 << 4) | (0 << 6) | ((8192 / (32 * 2)) << 8);
		}
	}

	CPUERR = MMR0 = MMR1 = MMR2 = MMR3 = PIR = CSR = 0;
}

bus::~bus()
{
	delete c;
	delete tm11;
	delete rk05_;
	delete rx02_;
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

uint16_t bus::read(const uint16_t a, const bool word_mode, const bool use_prev)
{
	uint16_t temp = 0;

	int run_mode = c->getPSW() >> 14;

	if (a >= 0160000) {
		if (word_mode)
			fprintf(stderr, "READ I/O %06o in byte mode\n", a);

		if (a == 0177750) { // MAINT
			D(fprintf(stderr, "read MAINT\n");)
			return 1; // POWER OK
		}

		if (a == 0177570) { // console switch & display register
			D(fprintf(stderr, "read console switch\n");)
			return 128; // educated guess
		}

		if (a == 0172540) { // KW11P programmable clock
			D(fprintf(stderr, "read programmable clock\n");)
			return 128;
		}

		if (a == 0177772) { // PIR
			D(fprintf(stderr, "read PIT\n");)
			return PIR;
		}

		if (a == 0177546) { // line frequency clock and status register
			D(fprintf(stderr, "read line freq clock\n");)
			CSR |= 128;
			return CSR;
		}

		if (a == 0177514) { // printer, CSR register, LP11
			D(fprintf(stderr, "read LP11 CSR\n");)
			return 0x80;
		}

		/// MMU ///
		if (a >= 0172200 && a < 0172220) {
			uint16_t t = pages[001][((a & 017) >> 1)].pdr;
			D(fprintf(stderr, "read supervisor I PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172220 && a < 0172240) {
			uint16_t t = pages[001][((a & 017) >> 1) + 8].pdr;
			D(fprintf(stderr, "read supervisor D PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172240 && a < 0172260) {
			uint16_t t = pages[001][((a & 017) >> 1)].par;
			D(fprintf(stderr, "read supervisor I PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172260 && a < 0172300) {
			uint16_t t = pages[001][((a & 017) >> 1) + 8].par;
			D(fprintf(stderr, "read supervisor D PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}

		if (a >= 0172300 && a < 0172320) {
			uint16_t t = pages[000][((a & 017) >> 1)].pdr;
			D(fprintf(stderr, "read kernel I PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172320 && a < 0172340) {
			uint16_t t = pages[000][((a & 017) >> 1) + 8].pdr;
			D(fprintf(stderr, "read kernel D PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172340 && a < 0172360) {
			uint16_t t = pages[000][((a & 017) >> 1)].par;
			D(fprintf(stderr, "read kernel I PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172360 && a < 0172400) {
			uint16_t t = pages[000][((a & 017) >> 1) + 8].par;
			D(fprintf(stderr, "read kernel D PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}

		if (a >= 0177600 && a < 0177620) {
			uint16_t t = pages[003][((a & 017) >> 1)].pdr;
			D(fprintf(stderr, "read userspace I PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0177620 && a < 0177640) {
			uint16_t t = pages[003][((a & 017) >> 1) + 8].pdr;
			D(fprintf(stderr, "read userspace D PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0177640 && a < 0177660) {
			uint16_t t = pages[003][((a & 017) >> 1)].par;
			D(fprintf(stderr, "read userspace I PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0177660 && a < 0177700) {
			uint16_t t = pages[003][((a & 017) >> 1) + 8].par;
			D(fprintf(stderr, "read userspace D PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		///////////

		if (word_mode) {
			if (a == 0177776) { // PSW
				D(fprintf(stderr, "readb PSW LSB\n");)
				return c -> getPSW() & 255;
			}

			if (a == 0177777) {
				D(fprintf(stderr, "readb PSW MSB\n");)
				return c -> getPSW() >> 8;
			}

			if (a == 0177774) { // stack limit register
				D(fprintf(stderr, "readb stack limit register\n");)
				return c -> getStackLimitRegister() & 0xff;
			}
			if (a == 0177775) { // stack limit register
				D(fprintf(stderr, "readb stack limit register\n");)
				return c -> getStackLimitRegister() >> 8;
			}

			if (a >= 0177700 && a <= 0177705) { // kernel R0-R5
				D(fprintf(stderr, "readb kernel R%d\n", a - 0177700);)
				return c -> getRegister(false, a - 0177700) & 0xff;
			}
			if (a >= 0177710 && a <= 0177715) { // user R0-R5
				D(fprintf(stderr, "readb user R%d\n", a - 0177710);)
				return c -> getRegister(true, a - 0177710) & 0xff;
			}
			if (a == 0177706) { // kernel SP
				D(fprintf(stderr, "readb kernel sp\n");)
				return c -> getStackPointer(0) & 0xff;
			}
			if (a == 0177707) { // PC
				D(fprintf(stderr, "readb pc\n");)
				return c -> getPC() & 0xff;
			}
			if (a == 0177716) { // supervisor SP
				D(fprintf(stderr, "readb supervisor sp\n");)
				return c -> getStackPointer(1) & 0xff;
			}
			if (a == 0177717) { // user SP
				D(fprintf(stderr, "readb user sp\n");)
				return c -> getStackPointer(3) & 0xff;
			}

			if (a == 0177766) { // cpu error register
				D(fprintf(stderr, "readb cpuerr\n");)
				return CPUERR & 0xff;
			}
		}
		else {
			if (a == 0177572) {
				D(fprintf(stderr, "read MMR0\n");)
				return MMR0;
			}

			if (a == 0177574) { // MMR1
				D(fprintf(stderr, "read MMR1\n");)
				return MMR1;
			}

			if (a == 0177576) { // MMR2
				D(fprintf(stderr, "read MMR2\n");)
				return MMR2;
			}

			if (a == 0172516) { // MMR3
				D(fprintf(stderr, "read MMR3\n");)
				return MMR3;
			}

			if (a == 0177776) { // PSW
				D(fprintf(stderr, "read PSW\n");)
				return c -> getPSW();
			}

			if (a == 0177774) { // stack limit register
				return c -> getStackLimitRegister();
			}

			if (a >= 0177700 && a <= 0177705) { // kernel R0-R5
				D(fprintf(stderr, "read kernel R%d\n", a - 0177700);)
				return c -> getRegister(false, a - 0177700);
			}
			if (a >= 0177710 && a <= 0177715) { // user R0-R5
				D(fprintf(stderr, "read user R%d\n", a - 0177710);)
				return c -> getRegister(true, a - 0177710);
			}
			if (a == 0177706) { // kernel SP
				D(fprintf(stderr, "read kernel sp\n");)
				return c -> getStackPointer(0);
			}
			if (a == 0177707) { // PC
				D(fprintf(stderr, "read pc\n");)
				return c -> getPC();
			}
			if (a == 0177716) { // supervisor SP
				D(fprintf(stderr, "read supervisor sp\n");)
				return c -> getStackPointer(1);
			}
			if (a == 0177717) { // user SP
				D(fprintf(stderr, "read user sp\n");)
				return c -> getStackPointer(3);
			}

			if (a == 0177766) { // cpu error register
				D(fprintf(stderr, "read CPUERR\n");)
				return CPUERR;
			}
		}

		if (tm11 && a >= TM_11_BASE && a < TM_11_END)
			return word_mode ? tm11 -> readByte(a) : tm11 -> readWord(a);

		if (rk05_ && a >= RK05_BASE && a < RK05_END)
			return word_mode ? rk05_ -> readByte(a) : rk05_ -> readWord(a);

		if (tty_ && a >= PDP11TTY_BASE && a < PDP11TTY_END)
			return word_mode ? tty_ -> readByte(a) : tty_ -> readWord(a);

		// LO size register field must be all 1s, so subtract 1
		const uint32_t system_size = n_pages * 8192 / 64 - 1;

		if (a == 0177762)  // system size HI
			return system_size >> 16;

		if (a == 0177760)  // system size LO
			return system_size & 65535;

		if (a & 1)
			D(fprintf(stderr, "bus::readWord: odd address UNHANDLED %o\n", a);)

		D(fprintf(stderr, "UNHANDLED read %o(%c)\n", a, word_mode ? 'B' : ' ');)

//		c -> busError();

		return -1;
	}

	const uint8_t apf = a >> 13; // active page field

	if (use_prev)
		run_mode = (c->getPSW() >> 12) & 3;

	uint32_t m_offset = pages[run_mode][apf].par * 64;  // memory offset

	uint16_t p_offset = a & 8191;  // page offset

	m_offset += p_offset;

	if (MMR0 & 1) {
		uint16_t pdr_len = (((pages[run_mode][apf].pdr >> 8) & 127) + 1) * 64;

		bool direction = pages[run_mode][apf].pdr & 8;

		if (m_offset >= n_pages * 8192) {
			D(fprintf(stderr, "bus::read %o >= %o\n", m_offset, n_pages * 8192);)
			c->schedule_trap(04);  // invalid address

			pages[run_mode][apf].pdr |= 1 << 7;

			throw 1;
		}

		if ((p_offset >= pdr_len && direction == false) || (p_offset < pdr_len && direction == true)) {
			D(fprintf(stderr, "bus::read::p_offset %o >= %o\n", p_offset, pdr_len);)
			c->schedule_trap(0250);  // invalid access

			pages[run_mode][apf].pdr |= 1 << 7;

			throw 1;
		}
	}

	if (word_mode)
		temp = m -> readByte(m_offset);
	else
		temp = m -> readWord(m_offset);

//	D(fprintf(stderr, "BUS read from %o (pages: %o/%o/%o, run mode %d, apf %d, PDR: %06o, b22: %d): %06o\n", m_offset, pages[run_mode][apf].par, pages[run_mode][apf].par * 64, n_pages * 8192, run_mode, apf, pages[run_mode][apf].pdr, MMR3 & 16, temp);)

	return temp;
}

uint32_t bus::calculate_full_address(const uint16_t a)
{
	const uint8_t apf = a >> 13; // active page field

	int run_mode = c->getPSW() >> 14;

	uint32_t m_offset = pages[run_mode][apf].par * 64;

	return m_offset + (a & 8191);
}

uint16_t bus::write(const uint16_t a, const bool word_mode, uint16_t value, const bool use_prev)
{
	int run_mode = c->getPSW() >> 14;

	if (a >= 0160000) {
		if (word_mode) {
			if (a == 0177776 || a == 0177777) { // PSW
				D(fprintf(stderr, "writeb PSW %s\n", a & 1 ? "MSB" : "LSB");)
				uint16_t vtemp = c -> getPSW();

				if (a & 1)
					vtemp = (vtemp & 0x00ff) | (value << 8);
				else
					vtemp = (vtemp & 0xff00) | value;

				c -> setPSW(vtemp);

				return value;
			}

			if (a == 0177774 || a == 0177775) { // stack limit register
				D(fprintf(stderr, "writeb Set stack limit register: %o\n", value);)
					uint16_t v = c -> getStackLimitRegister();

				if (a & 1)
					v = (v & 0xff00) | value;
				else
					v = (v & 0x00ff) | (value << 8);

				c -> setStackLimitRegister(v);
				return v;
			}
		}
		else {
			if (a == 0177776) { // PSW
				D(fprintf(stderr, "write PSW %o\n", value);)
					c -> setPSW(value);
				return value;
			}

			if (a == 0177774) { // stack limit register
				D(fprintf(stderr, "write Set stack limit register: %o\n", value);)
				c -> setStackLimitRegister(value);
				return value;
			}

			if (a >= 0177700 && a <= 0177705) { // kernel R0-R5
				D(fprintf(stderr, "write kernel R%d: %o\n", a - 01777700, value);)
				c -> setRegister(false, a - 0177700, value);
				return value;
			}
			if (a >= 0177710 && a <= 0177715) { // user R0-R5
				D(fprintf(stderr, "write user R%d: %o\n", a - 01777710, value);)
				c -> setRegister(true, a - 0177710, value);
				return value;
			}
			if (a == 0177706) { // kernel SP
				D(fprintf(stderr, "write kernel SP: %o\n", value);)
				c -> setStackPointer(0, value);
				return value;
			}
			if (a == 0177707) { // PC
				D(fprintf(stderr, "write PC: %o\n", value);)
				c -> setPC(value);
				return value;
			}
			if (a == 0177716) { // supervisor SP
				D(fprintf(stderr, "write supervisor sp: %o\n", value);)
				c -> setStackPointer(1, value);
				return value;
			}
			if (a == 0177717) { // user SP
				D(fprintf(stderr, "write user sp: %o\n", value);)
				c -> setStackPointer(3, value);
				return value;
			}

			if (a == 0177770) {  // microprogram break register
				return value;
			}
		}

		if (a == 0177766) { // cpu error register
			D(fprintf(stderr, "write CPUERR: %o\n", value);)
			CPUERR = 0;
			return CPUERR;
		}

		if (a == 0172516) { // MMR3
			D(fprintf(stderr, "write set MMR3: %o\n", value);)
			MMR3 = value & 067;
			return MMR3;
		}

		if (a == 0177576) { // MMR2
			D(fprintf(stderr, "write set MMR2: %o\n", value);)
			MMR2 = value;
			return MMR2;
		}

		if (a == 0177574) { // MMR1
			D(fprintf(stderr, "write set MMR1: %o\n", value);)
			MMR1 = value;
			return MMR1;
		}

		if (a == 0177572) { // MMR0
			D(fprintf(stderr, "write set MMR0: %o\n", value);)
			MMR0 = value & ~(3 << 10);  // bit 10 & 11 always read as 0
			return MMR0;
		}

		if (a == 0177772) { // PIR
			D(fprintf(stderr, "write set PIR: %o\n", value);)
			PIR = value; // FIXME
			return PIR;
		}

		if (a == 0177546) { // line frequency clock and status register
			D(fprintf(stderr, "write set LFC/SR: %o\n", value);)
			CSR = value;
			return CSR;
		}

		if (tm11 && a >= TM_11_BASE && a < TM_11_END) {
			word_mode ? tm11 -> writeByte(a, value) : tm11 -> writeWord(a, value);
			return value;
		}

		if (rk05_ && a >= RK05_BASE && a < RK05_END) {
			word_mode ? rk05_ -> writeByte(a, value) : rk05_ -> writeWord(a, value);
			return value;
		}

		if (tty_ && a >= PDP11TTY_BASE && a < PDP11TTY_END) {
			word_mode ? tty_ -> writeByte(a, value) : tty_ -> writeWord(a, value);
			return value;
		}

		/// MMU ///
		// supervisor
		if (a >= 0172200 && a < 0172220) {
			uint16_t t = pages[001][((a & 017) >> 1)].pdr = value;
			D(fprintf(stderr, "write supervisor I PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172220 && a < 0172240) {
			uint16_t t = pages[001][((a & 017) >> 1) + 8].pdr = value;
			D(fprintf(stderr, "write supervisor D PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172240 && a < 0172260) {
			uint16_t t = pages[001][((a & 017) >> 1)].par = value;
			D(fprintf(stderr, "write supervisor I PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172260 && a < 0172300) {
			uint16_t t = pages[001][((a & 017) >> 1) + 8].par = value;
			D(fprintf(stderr, "write supervisor D PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}

		// kernel
		if (a >= 0172300 && a < 0172320) {
			uint16_t t = pages[000][((a & 017) >> 1)].pdr = value;
			D(fprintf(stderr, "write kernel I PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172320 && a < 0172340) {
			uint16_t t = pages[000][((a & 017) >> 1) + 8].pdr = value;
			D(fprintf(stderr, "write kernel D PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172340 && a < 0172360) {
			uint16_t t = pages[000][((a & 017) >> 1)].par = value;
			D(fprintf(stderr, "write kernel I PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0172360 && a < 0172400) {
			uint16_t t = pages[000][((a & 017) >> 1) + 8].par = value;
			D(fprintf(stderr, "write kernel D PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}

		// user
		if (a >= 0177600 && a < 0177620) {
			uint16_t t = pages[003][((a & 017) >> 1)].pdr = value;
			D(fprintf(stderr, "write userspace I PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0177620 && a < 0177640) {
			uint16_t t = pages[003][((a & 017) >> 1) + 8].pdr = value;
			D(fprintf(stderr, "write userspace D PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0177640 && a < 0177660) {
			uint16_t t = pages[003][((a & 017) >> 1)].par = value;
			D(fprintf(stderr, "write userspace I PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		if (a >= 0177660 && a < 0177700) {
			uint16_t t = pages[003][((a & 017) >> 1) + 8].par = value;
			D(fprintf(stderr, "write userspace D PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}
		////

		if (a == 0177746) { // cache control register
			// FIXME
			return value;
		}

		if (a == 0177570) {  // switch register
			switch_register = value;
			return value;
		}

		///////////

		if (a == 0177374) { // FIXME
			fprintf(stderr, "char: %c\n", value & 127);
			return 128;
		}

		if (a & 1)
			D(fprintf(stderr, "bus::writeWord: odd address UNHANDLED\n");)

		D(fprintf(stderr, "UNHANDLED write %o(%c): %o\n", a, word_mode ? 'B' : ' ', value);)

//		c -> busError();

		return value;
	}

	const uint8_t apf = a >> 13; // active page field

	if (use_prev)
		run_mode = (c->getPSW() >> 12) & 3;

	uint32_t m_offset = pages[run_mode][apf].par * 64;

	pages[run_mode][apf].pdr |= 1 << 6; // page has been written to

	uint16_t p_offset = a & 8191;  // page offset

	m_offset += p_offset;

	if (MMR0 & 1) {
		uint16_t pdr_len = (((pages[run_mode][apf].pdr >> 8) & 127) + 1)* 64;

		bool direction = pages[run_mode][apf].pdr & 8;

		if (m_offset >= n_pages * 8192) {
			D(fprintf(stderr, "bus::write %o >= %o\n", m_offset, n_pages * 8192);)
			c->schedule_trap(04);  // invalid address

			pages[run_mode][apf].pdr |= 1 << 7;

			throw 1;
		}

		if ((p_offset >= pdr_len && direction == false) || (p_offset < pdr_len && direction == true)) {
			D(fprintf(stderr, "bus::write::p_offset %o >= %o\n", p_offset, pdr_len);)
			c->schedule_trap(0250);  // invalid access

			pages[run_mode][apf].pdr |= 1 << 7;

			throw 1;
		}
	}

//	D(fprintf(stderr, "BUS write to %o (pages: %o/%o/%o, run mode %d, apf %d, PDR: %06o, b22: %d): %06o\n", m_offset, pages[run_mode][apf].par, pages[run_mode][apf].par * 64, n_pages * 8192, run_mode, apf, pages[run_mode][apf].pdr, MMR3 & 16, value);)

	if (word_mode)
		m->writeByte(m_offset, value);
	else
		m->writeWord(m_offset, value);

	return value;
}

uint16_t bus::readWord(const uint16_t a)
{
	return read(a, false);
}

uint16_t bus::writeWord(const uint16_t a, const uint16_t value)
{
	return write(a, false, value);
}

uint16_t bus::readUnibusByte(const uint16_t a)
{
	return m->readByte(a);
}

void bus::writeUnibusByte(const uint16_t a, const uint8_t v)
{
	m->writeByte(a, v);
}
