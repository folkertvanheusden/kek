// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#if defined(ESP32)
#include <Arduino.h>
#endif
#include <assert.h>
#include <stdio.h>

#include "bus.h"
#include "gen.h"
#include "cpu.h"
#include "memory.h"
#include "tm-11.h"
#include "tty.h"

bus::bus() : c(nullptr), tm11(nullptr), rk05_(nullptr), rx02_(nullptr), tty_(nullptr)
{
#if defined(ESP32)
	// ESP32 goes in a crash-loop when allocating 128kB
	// see also https://github.com/espressif/esp-idf/issues/1934
	int n = 14;
#else
	int n = 16;
#endif

	m = new memory(n * 8192);

	for(int i=0; i<n; i++) {
		pages[i].par = (i & 7) * 8192 / 64;
		pages[i].pdr = (3 << 1) | (0 << 4) | (0 << 6) | ((8192 / (32 * 2)) << 8);
	}

	CPUERR = MMR2 = MMR3 = PIR = CSR = 0;
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

uint16_t bus::read(const uint16_t a, const bool word_mode, const bool use_prev)
{
	uint16_t temp = 0;

	if (a >= 0160000) {
		D(fprintf(stderr, "read%c I/O %o\n", word_mode ? 'b' : ' ', a);)

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
			return CSR;
		}

		if (a == 0177514) { // printer, CSR register, LP11
			D(fprintf(stderr, "read LP11 CSR\n");)
			return 0x80;
		}

		if (a == 0177564) { // console tty status register
			D(fprintf(stderr, "console tty status register\n");)
			return 0x80;
		}

		/// MMU ///
		if (a >= 0172300 && a < 0172320) {
			uint16_t t = pages[((a & 017) >> 1)].pdr;
			D(fprintf(stderr, "read PDR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}

		if (a >= 0172340 && a < 0172360) {
			uint16_t t = pages[((a & 017) >> 1)].par;
			D(fprintf(stderr, "read PAR for %d: %o\n", (a & 017) >> 1, t);)
			return t;
		}

		if (a >= 0177600 && a < 0177620) {
			uint16_t t = pages[((a & 017) >> 1) + 8].pdr;
			D(fprintf(stderr, "read PDR for %d: %o\n", ((a & 017) >> 1) + 8, t);)
			return t;
		}

		if (a >= 0177640 && a < 0177660) {
			uint16_t t = pages[((a & 017) >> 1) + 8].par;
			D(fprintf(stderr, "read PAR for %d: %o\n", ((a & 017) >> 1) + 8, t);)
			return t;
		}

		if (a == 0177572) {
			uint16_t t = ((c -> getRunMode() ? 0b11 : 0b00) << 5) | // kernel == 00
				((c -> getRegister(7) >> 13) << 1) | // page nr
				0 // MMU enabled
				;
			D(fprintf(stderr, "read MMU SR0 %o\n", t);)
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

		if (a & 1)
			D(fprintf(stderr, "bus::readWord: odd address UNHANDLED %o\n", a);)
		D(fprintf(stderr, "UNHANDLED read %o(%c)\n", a, word_mode ? 'B' : ' ');)

		c -> busError();

		return -1;
	}

	const uint8_t apf = a >> 13; // active page field
	bool is_user = use_prev ? (c -> getBitPSW(12) && c -> getBitPSW(13)) : (c -> getBitPSW(14) && c -> getBitPSW(15));
	D(fprintf(stderr, "READ: is_user %d, offset %d\n", is_user, apf + is_user * 8);)
	uint32_t m_offset = pages[apf + is_user * 8].par * 64;

	if ((a & 1) && word_mode == 0)
		D(fprintf(stderr, "odd addressing\n");)

	D(fprintf(stderr, "READ FROM %o\n", m_offset);)
	if (!word_mode)
		temp = m -> readWord(m_offset + (a & 8191));
	else
		temp = m -> readByte(m_offset + (a & 8191));

	//	D(fprintf(stderr, "read bus %o(%d): %o\n", a, word_mode, temp);)

	return temp;
}

uint16_t bus::write(const uint16_t a, const bool word_mode, uint16_t value, const bool use_prev)
{
	//D(fprintf(stderr, "write bus %o(%d): %o\n", a, word_mode, value);)

	assert(word_mode == 0 || value < 256);

	if (a >= 0160000) {
		D(fprintf(stderr, "write%c %o to I/O %o\n", word_mode ? 'b' : ' ', value, a);)

		if (word_mode) {
			if (a == 0177776 || a == 0177777) { // PSW
				D(fprintf(stderr, "writeb PSW %s\n", a & 1 ? "MSB" : "LSB");)
				assert(value < 256);
				uint16_t vtemp = c -> getPSW();

				if (a & 1)
					vtemp = (vtemp & 0x00ff) | (value << 8);
				else
					vtemp = (vtemp & 0xff00) | value;

				c -> setPSW(vtemp);

				return value;
			}

			if (a == 0177774 || a == 0177775) { // stack limit register
				D(fprintf(stderr, "writeb Set stack limit register to %o\n", value);)
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
				D(fprintf(stderr, "write Set stack limit register to %o\n", value);)
				c -> setStackLimitRegister(value);
				return value;
			}

			if (a >= 0177700 && a <= 0177705) { // kernel R0-R5
				D(fprintf(stderr, "write kernel R%d to %o\n", a - 01777700, value);)
				c -> setRegister(false, a - 0177700, value);
				return value;
			}
			if (a >= 0177710 && a <= 0177715) { // user R0-R5
				D(fprintf(stderr, "write user R%d to %o\n", a - 01777710, value);)
				c -> setRegister(true, a - 0177710, value);
				return value;
			}
			if (a == 0177706) { // kernel SP
				D(fprintf(stderr, "write kernel SP to %o\n", value);)
				c -> setStackPointer(0, value);
				return value;
			}
			if (a == 0177707) { // PC
				D(fprintf(stderr, "write PC to %o\n", value);)
				c -> setPC(value);
				return value;
			}
			if (a == 0177716) { // supervisor SP
				D(fprintf(stderr, "write supervisor sp to %o\n", value);)
				c -> setStackPointer(1, value);
				return value;
			}
			if (a == 0177717) { // user SP
				D(fprintf(stderr, "write user sp to %o\n", value);)
				c -> setStackPointer(3, value);
				return value;
			}
		}

		if (a == 0177766) { // cpu error register
			D(fprintf(stderr, "write CPUERR %o\n", value);)
			CPUERR = 0;
			return CPUERR;
		}

		if (a == 0172516) { // MMR3
			D(fprintf(stderr, "write set MMR3 to %o\n", value);)
			MMR3 = value;
			return MMR3;
		}

		if (a == 0177772) { // PIR
			D(fprintf(stderr, "write set PIR to %o\n", value);)
			PIR = value; // FIXME
			return PIR;
		}

		if (a == 0177546) { // line frequency clock and status register
			D(fprintf(stderr, "write set LFC/SR to %o\n", value);)
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
		if (a >= 0172300 && a < 0172320) {
			D(fprintf(stderr, "write set PDR for %d to %o\n", (a & 017) >> 1, value);)
			pages[((a & 017) >> 1)].pdr = value;
			return value;
		}

		if (a >= 0172340 && a < 0172360) {
			D(fprintf(stderr, "write set PAR for %d to %o\n", (a & 017) >> 1, value);)
			pages[((a & 017) >> 1)].par = value;
			return value;
		}

		if (a >= 0117600 && a < 0117620) {
			D(fprintf(stderr, "write set PDR for %d to %o\n", ((a & 017) >> 1) + 8, value);)
			pages[((a & 017) >> 1) + 8].pdr = value;
			return value;
		}

		if (a >= 0117640 && a < 0177660) {
			D(fprintf(stderr, "write set PAR for %d to %o\n", ((a & 017) >> 1) + 8, value);)
			pages[((a & 017) >> 1) + 8].par = value;
			return value;
		}

		if (a == 0177746) { // cache control register
			// FIXME
			return value;
		}

		///////////

		if (a == 0177374) { // FIXME
			fprintf(stderr, "char: %c\n", value & 127);
			return 128;
		}

		if (a == 0177566) { // console tty buffer register
			D(fprintf(stderr, "bus::write TTY buffer %d / %c\n", value, value);)

			if (value) {
#if defined(ESP32)
				Serial.print(char(value & 127));
#else
				printf("%c", value & 127);
#endif
			}

			return 128;
		}

		if (a & 1)
			D(fprintf(stderr, "bus::writeWord: odd address UNHANDLED\n");)
		D(fprintf(stderr, "UNHANDLED write %o(%c): %o\n", a, word_mode ? 'B' : ' ', value);)

		c -> busError();

		return value;
	}

	const uint8_t apf = a >> 13; // active page field
	bool is_user = use_prev ? (c -> getBitPSW(12) && c -> getBitPSW(13)) : (c -> getBitPSW(14) && c -> getBitPSW(15));
	D(fprintf(stderr, "WRITE: is_user %d, offset %d\n", is_user, apf + is_user * 8);)
	uint32_t m_offset = pages[apf + is_user * 8].par * 64;

	pages[apf].pdr |= 1 << 6; // page has been written to

	if ((a & 1) && word_mode == 0)
		D(fprintf(stderr, "odd addressing\n");)

	D(fprintf(stderr, "WRITE TO: %o\n", m_offset);)
	if (word_mode)
		m -> writeByte(m_offset + (a & 8191), value);
	else
		m -> writeWord(m_offset + (a & 8191), value);

	return value;
}

uint16_t bus::readWord(const uint16_t a)
{
	return read(a, false);
}

uint16_t bus::writeWord(const uint16_t a, const uint16_t value)
{
	write(a, false, value);

	return value;
}
