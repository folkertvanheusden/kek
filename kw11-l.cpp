// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <unistd.h>

#include "console.h"
#include "cpu.h"
#include "kw11-l.h"
#include "log.h"
#include "utils.h"

#if defined(ESP32)
#include "esp32.h"
#elif defined(BUILD_FOR_RP2040)
#include "rp2040.h"
#endif


#if defined(ESP32) || defined(BUILD_FOR_RP2040)
void thread_wrapper_kw11(void *p)
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

#if !defined(ESP32) && !defined(BUILD_FOR_RP2040)
	if (th) {
		th->join();

		delete th;
	}
#endif
}

void kw11_l::show_state(console *const cnsl) const
{
	cnsl->put_string_lf(format("CSR: %06o", lf_csr));
}

void kw11_l::begin(console *const cnsl)
{
	this->cnsl = cnsl;

#if defined(ESP32) || defined(BUILD_FOR_RP2040)
	xTaskCreate(&thread_wrapper_kw11, "kw11-l", 2048, this, 1, nullptr);

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(lf_csr_lock);  // initialize
#endif
#else
	th = new std::thread(std::ref(*this));
#endif
}

void kw11_l::reset()
{
	lf_csr = 0;
}

void kw11_l::do_interrupt()
{
	set_lf_crs_b7();

	if (get_lf_crs() & 64)
		b->getCpu()->queue_interrupt(6, 0100);
}

void kw11_l::operator()()
{
	set_thread_name("kek:kw-11l");

	TRACE("Starting KW11-L thread");

	uint64_t prev_cycle_count          = b->getCpu()->get_instructions_executed_count();
	uint64_t interval_prev_cycle_count = prev_cycle_count;
	auto     prev_tick                 = get_ms();

	while(!stop_flag) {
		if (*cnsl->get_running_flag()) {
			myusleep(1000000 / 100);  // 100 Hz

			uint64_t current_cycle_count = b->getCpu()->get_instructions_executed_count();
			uint32_t took_ms = b->getCpu()->get_effective_run_time(current_cycle_count - prev_cycle_count);
			auto     now     = get_ms();

			// - 50 Hz depending on instrution count
			// - nothing executed in interval
			// - 10 Hz minimum
			if (took_ms >= 1000 / 50 || current_cycle_count - interval_prev_cycle_count == 0 || now - prev_tick >= 100) {
				do_interrupt();

				prev_cycle_count = current_cycle_count;

				prev_tick        = now;
			}

			interval_prev_cycle_count = current_cycle_count;
		}
		else {
			myusleep(1000000 / 10);  // 10 Hz
		}
	}

	TRACE("KW11-L thread terminating");
}

uint16_t kw11_l::read_word(const uint16_t a)
{
	if (a != ADDR_LFC) {
		TRACE("KW11-L read_word not for us (%06o)", a);
		return 0;
	}

#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(lf_csr_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(lf_csr_lock);
#endif

	uint16_t temp = lf_csr;

#if defined(BUILD_FOR_RP2040)
       	xSemaphoreGive(lf_csr_lock);
#endif

	return temp;
}

void kw11_l::write_byte(const uint16_t addr, const uint8_t value)
{
	if (addr != ADDR_LFC) {
		TRACE("KW11-L write_byte not for us (%06o to %06o)", value, addr);
		return;
	}

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

#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(lf_csr_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(lf_csr_lock);
#endif

	TRACE("WRITE-I/O set line frequency clock/status register: %06o", value);
	lf_csr = value;
#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(lf_csr_lock);
#endif
}

void kw11_l::set_lf_crs_b7()
{
#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(lf_csr_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(lf_csr_lock);
#endif

	lf_csr |= 128;

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(lf_csr_lock);
#endif
}

uint8_t kw11_l::get_lf_crs()
{
#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(lf_csr_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(lf_csr_lock);
#endif

	uint8_t rc = lf_csr;

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(lf_csr_lock);
#endif

	return rc;
}

#if IS_POSIX
json_t *kw11_l::serialize()
{
	json_t *j = json_object();

	json_object_set(j, "CSR", json_integer(lf_csr));

	return j;
}

kw11_l *kw11_l::deserialize(const json_t *const j, bus *const b, console *const cnsl)
{
	uint16_t CSR = json_integer_value(json_object_get(j, "CSR"));

	kw11_l *out  = new kw11_l(b);
	out->lf_csr  = CSR;
	out->begin(cnsl);

	return out;
}
#endif
