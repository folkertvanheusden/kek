// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "tty.h"
#include "cpu.h"
#include "gen.h"
#include "log.h"
#include "memory.h"
#include "utils.h"


const char * const regnames[] = { 
	"reader status ",
	"reader buffer ",
	"puncher status",
	"puncher buffer"
	};

tty::tty(console *const c, bus *const b) :
	c(c),
	b(b)
{
	reset(true);
	c->set_data_cb_notifier(this);
}

tty::~tty()
{
}

void tty::reset(const bool hard)
{
	if (hard)
		memset(registers, 0x00, sizeof registers);
}

uint8_t tty::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr & ~1);
	if (addr & 1)
		return v >> 8;
	return v;
}

void tty::notify_rx()
{
	registers[(PDP11TTY_TKS - PDP11TTY_BASE) / 2] |= 128;

	if (registers[(PDP11TTY_TKS - PDP11TTY_BASE) / 2] & 64)
		b->getCpu()->queue_interrupt(4, 060);
}

uint16_t tty::read_word(const uint16_t addr)
{
	const int reg    = (addr - PDP11TTY_BASE) / 2;
	uint16_t  vtemp  = registers[reg];
	bool      notify = false;

	if (addr == PDP11TTY_TKS) {
		bool have_char = c->poll_char();

		vtemp &= ~128;
		vtemp |= have_char ? 128 : 0;
	}
	else if (addr == PDP11TTY_TKB) {
		auto ch = c->wait_char(1);
		if (ch.has_value() == false)
			vtemp = 0;
		else {
			vtemp = ch.value() | (parity(ch.value()) << 7);
			if (c->poll_char())
				notify = true;
		}
	}
	else if (addr == PDP11TTY_TPS) {
		vtemp |= 128;
	}

	DOLOG(log_ss::LS_COMM, "PDP11TTY read addr %o (%s): %d, 7bit: %d", addr, regnames[reg], vtemp, vtemp & 127);

	registers[reg] = vtemp;

	if (notify)
		notify_rx();

	return vtemp;
}

void tty::write_byte(const uint16_t addr, const uint8_t v)
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

	write_word(addr, vtemp);
}

void tty::write_word(const uint16_t addr, uint16_t v)
{
	const int reg = (addr - PDP11TTY_BASE) / 2;

	DOLOG(log_ss::LS_COMM, "PDP11TTY write %o (%s): %o", addr, regnames[reg], v);

	if (addr == PDP11TTY_TPB) {
		char ch = v & 127;
		DOLOG(log_ss::LS_COMM, "PDP11TTY print '%c'", ch);
		c->put_char(ch);

		registers[(PDP11TTY_TPS - PDP11TTY_BASE) / 2] |= 128;

		if (registers[(PDP11TTY_TPS - PDP11TTY_BASE) / 2] & 64)
			b->getCpu()->queue_interrupt(4, 064);
	}

	DOLOG(log_ss::LS_COMM, "set register %o to %o", addr, v);
	registers[(addr - PDP11TTY_BASE) / 2] = v;
}

JsonDocument tty::serialize()
{
	JsonDocument j;

	JsonDocument ja_reg;
	JsonArray    ja_reg_work = ja_reg.to<JsonArray>();
        for(size_t i=0; i<4; i++)
                ja_reg_work.add(registers[i]);
	j["registers"] = ja_reg;

	return j;
}

tty *tty::deserialize(const JsonVariantConst j, bus *const b, console *const cnsl)
{
	tty       *out   = new tty(cnsl, b);

	JsonArrayConst ja_reg = j["registers"];
	int       i_reg  = 0;
	for(auto v: ja_reg)
		out->registers[i_reg++] = v;

	return out;
}
