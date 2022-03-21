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

tty::tty(std::function<bool()> poll_char, std::function<uint8_t()> get_char, std::function<void(char c)> put_char) :
	poll_char(poll_char),
	get_char(get_char),
	put_char(put_char)
{
#if defined(ESP32)
	queue = xQueueCreate(10, sizeof(char));

	if (queue == nullptr)
		Serial.println(F("Problem creating TTY queue"));
#endif
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

	if (have_char_1) {
		have_char_1 = false;
		have_char_2 = true;
	}
	else if (have_char_2 == false) {
		have_char_1 = poll_char();
	}

	if (addr == PDP11TTY_TKS) {
		vtemp = (have_char_2 ? 1 << 7 : 0) | (have_char_1 ? 1 << 11 : 0);
	}
	else if (addr == PDP11TTY_TKB) {
		if (have_char_2) {
			uint8_t c = get_char();

			vtemp = c | (parity(c) << 7);

			have_char_2 = false;
		}
		else {
			vtemp = 0;
		}
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

	if (addr == PDP11TTY_TPB) {
		char c = v & 127;

		D(fprintf(stderr, "PDP11TTY print '%c'\n", c);)

#if defined(ESP32)
		if (xQueueSend(queue, &c, portMAX_DELAY) != pdTRUE)
			Serial.println(F("queue TTY character failed"));
#else
		put_char(c);
#endif
	}

	D(fprintf(stderr, "set register %o to %o\n", addr, v);)
	registers[(addr - PDP11TTY_BASE) / 2] = v;
}
