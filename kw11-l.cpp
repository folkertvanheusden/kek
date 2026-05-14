// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <cinttypes>
#include <unistd.h>

#include "console.h"
#include "cpu.h"
#include "kw11-l.h"
#include "log.h"
#include "utils.h"


#if defined(ESP32) || defined(BUILD_FOR_PICO2W)
static void thread_wrapper_kw11(void *p)
{
	kw11_l *const kw11l = reinterpret_cast<kw11_l *>(p);

	kw11l->operator()();
}
#endif

kw11_l::kw11_l(bus *const b): b(b)
{
}

kw11_l::~kw11_l()
{
	stop_flag = true;

#if !defined(ESP32) && !defined(BUILD_FOR_PICO2W)
	if (th) {
		th->join();

		delete th;
	}
#endif
}

void kw11_l::show_state(console *const cnsl) const
{
	cnsl->put_string_lf(format("CSR: %06o", lf_csr));
	cnsl->put_string_lf(format("%" PRIu64 " total ticks, %" PRIu64 " while enabled, %" PRIu64 " interrupts", total_ticks, enabled_ticks, int_triggered));
}

void kw11_l::begin(console *const cnsl)
{
	this->cnsl = cnsl;

#if defined(ESP32) || defined(BUILD_FOR_PICO2W)
	xTaskCreate(&thread_wrapper_kw11, "kw11-l", 3072, this, 2, nullptr);
#else
	th = new std::thread(std::ref(*this));
#endif
}

void kw11_l::reset(const bool hard)
{
	if (hard) {
		my_unique_lock lck(&lc_csr_lock);
		lf_csr = 0;
	}
}

void kw11_l::do_interrupt()
{
	enabled_ticks++;
	set_lf_crs_b7();

	if (get_lf_crs() & 64) {
		int_triggered++;
		b->getCpu()->queue_interrupt(6, 0100);
	}
}

int kw11_l::get_interrupt_frequency()
{
	my_unique_lock lck(&lc_csr_lock);
	return int_frequency;
}

void kw11_l::operator()()
{
	set_thread_name("kek:kw-11l");

	TRACE("Starting KW11-L thread");

	while(!stop_flag) {
		total_ticks++;

		int f = 0;
		{
			my_unique_lock lck(&lc_csr_lock);
			f = int_frequency;
		}
		myusleep(1000000 / f);  // usually 50 or 60 Hz

		if (*cnsl->get_running_flag())
			do_interrupt();
	}

	TRACE("KW11-L thread terminating");
}

uint8_t kw11_l::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr & ~1);
	if (addr & 1)
		return v >> 8;
	return v;
}

uint16_t kw11_l::read_word(const uint16_t a)
{
	if (a != ADDR_LFC) {
		TRACE("KW11-L read_word not for us (%06o)", a);
		return 0;
	}

	my_unique_lock lck(&lc_csr_lock);

	return lf_csr;
}

void kw11_l::set_interrupt_frequency(const int Hz)
{
	my_unique_lock lck(&lc_csr_lock);
	int_frequency = Hz;
}

void kw11_l::write_byte(const uint16_t addr, const uint8_t value)
{
	if (addr != ADDR_LFC) {
		TRACE("KW11-L write_byte not for us (%06o to %06o)", value, addr);
		return;
	}

	my_unique_lock lck(&lc_csr_lock);

	uint16_t vtemp = lf_csr;
	
	if (addr & 1) {
		vtemp &= ~0xff00;
		vtemp |= value << 8;
	}
	else {
		vtemp &= ~0x00ff;
		vtemp |= value;
	}

	write_word(addr, vtemp);
}

void kw11_l::write_word(const uint16_t a, const uint16_t value)
{
	if (a != ADDR_LFC) {
		TRACE("KW11-L write_word not for us (%06o to %06o)", value, a);
		return;
	}

	my_unique_lock lck(&lc_csr_lock);

	TRACE("WRITE-I/O set line frequency clock/status register: %06o", value);
	lf_csr = value;
}

void kw11_l::set_lf_crs_b7()
{
	my_unique_lock lck(&lc_csr_lock);
	lf_csr |= 128;
}

uint8_t kw11_l::get_lf_crs()
{
	my_unique_lock lck(&lc_csr_lock);
	return lf_csr;
}

JsonDocument kw11_l::serialize()
{
	JsonDocument j;
	j["CSR"       ] = lf_csr;
	j["wall-clock"] = wall_clock;
	return j;
}

kw11_l *kw11_l::deserialize(const JsonVariantConst j, bus *const b, console *const cnsl)
{
	uint16_t CSR        = j["CSR"       ];
	bool     wall_clock = j["wall-clock"];

	kw11_l *out  = new kw11_l(b);
	out->lf_csr     = CSR;
	out->wall_clock = wall_clock;
	out->begin(cnsl);

	return out;
}
