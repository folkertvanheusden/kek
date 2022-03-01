// (C) 2018 by Folkert van Heusden
// Released under AGPL v3.0
#include <errno.h>
#include <string.h>

#include "rx02.h"
#include "gen.h"
#include "memory.h"
#include "utils.h"

rx02::rx02(const std::string & file, memory *const m) : m(m)
{
	offset = 0;
	memset(registers, 0x00, sizeof registers);

	fh = fopen(file.c_str(), "rb");
}

rx02::~rx02()
{
	fclose(fh);
}

uint8_t rx02::readByte(const uint16_t addr)
{
	uint16_t v = readWord(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t rx02::readWord(const uint16_t addr)
{
	const int reg = (addr - RX02_BASE) / 2;
	uint16_t vtemp = registers[reg];

	D(printf("RX02 read addr %o: ", addr);)

	// FIXME

	D(printf("%o\n", vtemp);)

	return vtemp;
}

void rx02::writeByte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - RX02_BASE) / 2];

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

void rx02::writeWord(const uint16_t addr, uint16_t v)
{
	D(printf("RX02 write %o: %o\n", addr, v);)

	// FIXME

	D(printf("set register %o to %o\n", addr, v);)
	registers[(addr - RX02_BASE) / 2] = v;
}
