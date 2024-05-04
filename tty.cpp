// (C) 2018-2024 by Folkert van Heusden
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

#if defined(BUILD_FOR_RP2040)
void thread_wrapper_tty(void *p)
{
	tty *const t = reinterpret_cast<tty *>(p);

	t->operator()();
}
#endif

tty::tty(console *const c, bus *const b) :
	c(c),
	b(b)
{
	reset();

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(chars_lock);  // initialize
#endif

#if defined(BUILD_FOR_RP2040)
	xTaskCreate(&thread_wrapper_tty, "tty", 2048, this, 1, nullptr);
#else
	th = new std::thread(std::ref(*this));
#endif
}

tty::~tty()
{
	stop_flag = true;

#if !defined(BUILD_FOR_RP2040)
	th->join();
	delete th;
#endif
}

void tty::reset()
{
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

#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(chars_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(chars_lock);
#endif

	if (addr == PDP11TTY_TKS) {
		bool have_char = chars.empty() == false;

		vtemp &= ~128;
		vtemp |= have_char ? 128 : 0;
	}
	else if (addr == PDP11TTY_TKB) {
		if (chars.empty())
			vtemp = 0;
		else {
			uint8_t ch = chars.front();
			chars.erase(chars.begin());

			vtemp = ch | (parity(ch) << 7);

			if (chars.empty() == false)
				notify = true;
		}
	}
	else if (addr == PDP11TTY_TPS) {
		vtemp |= 128;
	}

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(chars_lock);
#endif

	TRACE("PDP11TTY read addr %o (%s): %d, 7bit: %d", addr, regnames[reg], vtemp, vtemp & 127);

	registers[reg] = vtemp;

	if (notify)
		notify_rx();

	return vtemp;
}

void tty::operator()()
{
	set_thread_name("kek:tty");

	while(!stop_flag) {
		if (c->poll_char()) {
#if defined(BUILD_FOR_RP2040)
			digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
			xSemaphoreTake(chars_lock, portMAX_DELAY);
#else
			std::unique_lock<std::mutex> lck(chars_lock);
#endif

			chars.push_back(c->get_char());

#if defined(BUILD_FOR_RP2040)
			xSemaphoreGive(chars_lock);
#endif

			notify_rx();
		}
		else {
			myusleep(100000);
		}
	}
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

	TRACE("PDP11TTY write %o (%s): %o", addr, regnames[reg], v);

	if (addr == PDP11TTY_TPB) {
		char ch = v & 127;

		TRACE("PDP11TTY print '%c'", ch);

		c->put_char(ch);

		registers[(PDP11TTY_TPS - PDP11TTY_BASE) / 2] |= 128;

		if (registers[(PDP11TTY_TPS - PDP11TTY_BASE) / 2] & 64)
			b->getCpu()->queue_interrupt(4, 064);
	}

	TRACE("set register %o to %o", addr, v);
	registers[(addr - PDP11TTY_BASE) / 2] = v;
}

#if IS_POSIX
json_t *tty::serialize()
{
	json_t *j = json_object();

        json_t *ja_reg = json_array();
        for(size_t i=0; i<4; i++)
                json_array_append(ja_reg, json_integer(registers[i]));
        json_object_set(j, "registers", ja_reg);

        json_t *ja_buf = json_array();
	for(auto & c: chars)
                json_array_append(ja_buf, json_integer(c));
        json_object_set(j, "input-buffer", ja_buf);

	return j;
}

tty *tty::deserialize(const json_t *const j, bus *const b, console *const cnsl)
{
	tty *out  = new tty(cnsl, b);

	json_t *ja_reg = json_object_get(j, "registers");
	for(size_t i=0; i<4; i++)
		out->registers[i] = json_integer_value(json_array_get(ja_reg, i));

	json_t *ja_buf   = json_object_get(j, "input-buffer");
	size_t  buf_size = json_array_size(ja_buf);
	for(size_t i=0; i<buf_size; i++)
		out->chars.push_back(json_integer_value(json_array_get(ja_buf, i)));

	return out;
}
#endif
