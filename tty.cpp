// (C) 2018 by Folkert van Heusden
// // Released under Apache License v2.0
#include <errno.h>
#if defined(ESP32)
#include <Arduino.h>
#else
#include <poll.h>
#endif
#include <string.h>
#include <unistd.h>

#include "tty.h"
#include "gen.h"
#include "memory.h"
#include "utils.h"

#if !defined(ESP32)
#include "terminal.h"
extern NEWWIN *w_main;
#endif

const char * const regnames[] = { 
	"reader status ",
	"reader buffer ",
	"puncher status",
	"puncher buffer"
	};

tty::tty(const bool withUI) : withUI(withUI)
{
	memset(registers, 0x00, sizeof registers);
}

tty::~tty()
{
}

uint8_t tty::readByte(const uint16_t addr)
{
	uint16_t v = readWord(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t tty::readWord(const uint16_t addr)
{
	const int reg = (addr - PDP11TTY_BASE) / 2;
	uint16_t vtemp = registers[reg];

	if (addr == PDP11TTY_TKS) {
		vtemp = c ? 128 : 0;

#if defined(ESP32)
		static bool led_state = true;
		digitalWrite(LED_BUILTIN, led_state);
		led_state = !led_state;
#endif
	}
	else if (addr == PDP11TTY_TKB) {
		vtemp = c | (parity(c) << 7);
		c = 0;
	}
	else if (addr == PDP11TTY_TPS) {
		vtemp = 128;
	}

	D(fprintf(stderr, "PDP11TTY read addr %o (%s): %d, 7bit: %d\n", addr, regnames[reg], vtemp, vtemp & 127);)

	registers[reg] = vtemp;

	return vtemp;
}

void tty::writeByte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - PDP11TTY_BASE) / 2];
	
	if (addr & 1) {
		vtemp &= ~0xff00;
		vtemp |= v << 8;
	}
	else {
		vtemp &= ~0x00ff;
		vtemp |= v;
	}

	writeWord(addr, vtemp);
}

void tty::writeWord(const uint16_t addr, uint16_t v)
{
	const int reg = (addr - PDP11TTY_BASE) / 2;

	D(fprintf(stderr, "PDP11TTY write %o (%s): %o\n", addr, regnames[reg], v);)

	if (v == 0207 && testMode) {
		D(fprintf(stderr, "TestMode: TTY 0207 char\n");)

#if !defined(ESP32)
		exit(0);
#endif
	}

	// FIXME
	if (addr == PDP11TTY_TPB) {
		v &= 127;

#if defined(ESP32)
		Serial.print(char(v));
#else
		FILE *tf = fopen("tty.dat", "a+");
		if (tf) {
			fprintf(tf, "%c", v);
			fclose(tf);
		}

		if (withUI) {
			if (v >= 32 || (v != 12 && v != 27 && v != 13)) {
				wprintw(w_main -> win, "%c", v);
				mydoupdate();
			}
		}
		else {
			printf("%c", v);
			fflush(NULL);
		}

		fprintf(stderr, "punch char: '%c'\n", v);
#endif
	}

	D(fprintf(stderr, "set register %o to %o\n", addr, v);)
	registers[(addr - PDP11TTY_BASE) / 2] = v;
}

void tty::sendChar(const char v)
{
#if !defined(ESP32)
	if (c)
		fprintf(stderr, "PDP11TTY: overwriting %d - %c\n", c, c);
	else
		fprintf(stderr, "PDP11TTY: setting character %d - %c\n", v, v);
#endif

	c = v;
}
