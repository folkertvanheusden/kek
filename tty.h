// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>

#include "console.h"


#define PDP11TTY_TKS		0777560	// reader status
#define PDP11TTY_TKB		0777562	// reader buffer
#define PDP11TTY_TPS		0777564	// puncher status
#define PDP11TTY_TPB		0777566	// puncher buffer
#define PDP11TTY_BASE	PDP11TTY_TKS
#define PDP11TTY_END	(PDP11TTY_TPB + 2)

class memory;

class tty
{
private:
	console *const c      { nullptr };
	bool     have_char_1  { false };  // RCVR BUSY bit high (11)
	bool     have_char_2  { false };  // RCVR DONE bit high (7)
	uint16_t registers[4] { 0 };

public:
	tty(console *const c);
	virtual ~tty();

	uint8_t  readByte(const uint32_t addr);
	uint16_t readWord(const uint32_t addr);

	void writeByte(const uint32_t addr, const uint8_t v);
	void writeWord(const uint32_t addr, uint16_t      v);
};
