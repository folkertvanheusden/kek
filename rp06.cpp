// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include <errno.h>
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "error.h"
#include "gen.h"
#include "log.h"
#include "rp06.h"
#include "utils.h"


const char *regnames[] { "Control", "Status", "Error register 1", "Maintenance", "Attention summary", "Desired sector/track address", "Look ahead", "Drive type", "Serial no", "Offset", "Desired cylinder address", "Current cylinder address", "Error register 2", "Error register 3", "ECC position", "ECC pattern" };

rp06::rp06(bus *const b, std::atomic_bool *const disk_read_activity, std::atomic_bool *const disk_write_activity) :
	b(b),
	disk_read_activity (disk_read_activity ),
	disk_write_activity(disk_write_activity)
{
}

rp06::~rp06()
{
}

void rp06::begin()
{
	reset();
}

void rp06::reset()
{
}

void rp06::show_state(console *const cnsl) const
{
}

JsonDocument rp06::serialize() const
{
	JsonDocument j;

	return j;
}

rp06 *rp06::deserialize(const JsonVariantConst j, bus *const b)
{
	rp06 *r = new rp06(b, nullptr, nullptr);
	r->begin();

	return r;
}

uint8_t rp06::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t rp06::read_word(const uint16_t addr)
{
	const int reg = (addr - RP06_BASE) / 2;

	uint16_t value = 0;


	TRACE("RP06: read \"%s\"/%o: %06o", regnames[reg], addr, value);

	return value;
}

void rp06::write_byte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - RP06_BASE) / 2];

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

void rp06::write_word(const uint16_t addr, uint16_t v)
{
	const int reg = (addr - RP06_BASE) / 2;

	TRACE("RP06: write \"%s\"/%06o: %06o", regnames[reg], addr, v);

        registers[reg] = v;

	if (reg == RP06_CR) {
		if (v & 1) {
			int function_code = (v >> 1) & 31;


		}
	}
}
