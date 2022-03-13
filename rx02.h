// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>

// FIXME namen van defines
#define RX02_BASE	0
#define RX02_END	(1 + 2)

class memory;

class rx02
{
private:
	memory *const m;
	uint16_t registers[7];
	uint8_t xfer_buffer[512];
	int offset;
	FILE *fh;

public:
	rx02(const std::string & file, memory *const m);
	virtual ~rx02();

	uint8_t readByte(const uint16_t addr);
	uint16_t readWord(const uint16_t addr);

	void writeByte(const uint16_t addr, const uint8_t v);
	void writeWord(const uint16_t addr, uint16_t v);
};
