// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "bus.h"
#include "dc11.h"


dc11::dc11(bus *const b): b(b)
{
}

dc11::~dc11()
{
	stop_flag = true;
}

void dc11::reset()
{
}

uint8_t dc11::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t dc11::read_word(const uint16_t addr)
{
	const int reg   = (addr - DC11_BASE) / 2;

	uint16_t  vtemp = registers[reg];

	DOLOG(debug, false, "DC11: read register %06o (%d): %06o", addr, reg, vtemp);

	return vtemp;
}

void dc11::write_byte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - DC11_BASE) / 2];
	
	if (addr & 1) {
		vtemp &= ~0xff00;
		vtemp |= v << 8;
	}
	else {
		vtemp &= ~0x00ff;
		vtemp |= v;
	}

	write_word(addr, vtemp);
}

void dc11::write_word(const uint16_t addr, uint16_t v)
{
	const int reg = (addr - DC11_BASE) / 2;

	DOLOG(debug, false, "DC11: write register %06o (%d) to %o", addr, reg, v);

	registers[reg] = v;
}
