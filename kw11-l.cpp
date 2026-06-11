// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#if defined(LOAD_GAUGE_PIN)
#include <Arduino.h>
#endif
#include <cinttypes>
#include <unistd.h>

#include "console.h"
#include "cpu.h"
#include "kw11-l.h"
#include "log.h"
#include "utils.h"

#if defined(ESP32)
static esp_timer_handle_t kw11l_periodic_timer { };
static void periodic_timer_callback(void *arg)
{
	auto p = reinterpret_cast<kw11_l *>(arg);
	p->tick();
}
#elif defined(TEENSY4_1)
static kw11_l *dev_p { nullptr };
static bool periodic_timer_callback(TimerHandle_t handle)
{
	dev_p->tick();
	return true;
}
#elif defined(BUILD_FOR_PICO2W)
static bool periodic_timer_callback(repeating_timer *arg)
{
	auto p = reinterpret_cast<kw11_l *>(arg->user_data);
	p->tick();
	return true;
}
#elif defined(FREERTOS)
static void thread_wrapper_kw11(void *p)
{
	kw11_l *const kw11l = reinterpret_cast<kw11_l *>(p);
	kw11l->operator()();
}
#endif

kw11_l::kw11_l(bus *const b): b(b)
{
#if defined(TEENSY4_1)
	dev_p = this;
#endif
}

kw11_l::~kw11_l()
{
	stop_flag = true;
#if defined(ESP32)
	esp_timer_delete(kw11l_periodic_timer);
#elif defined(TEENSY4_1)
	xTimerStop(timer, 1);
#elif defined(BUILD_FOR_PICO2W)
	cancel_repeating_timer(&timer);
#elif !defined(FREERTOS)
	if (th) {
		th->join();
		delete th;
	}
#endif
}

FLASHMEM void kw11_l::show_state(console *const cnsl) const
{
	cnsl->put_string_lf(format("CSR: %06o", lf_csr));
	cnsl->put_string_lf(format("%" PRIu64 " total ticks, %" PRIu64 " while enabled, %" PRIu64 " interrupts", total_ticks, enabled_ticks, int_triggered));
#if defined(LOAD_GAUGE_PIN)
	cnsl->put_string_lf(format("last instruction count (for load calculation): %" PRIu64 ", divider: %" PRIu64, last_instructions_count, max_instructions_count));
#endif
}

void kw11_l::begin(console *const cnsl)
{
	this->cnsl = cnsl;

#if defined(LOAD_GAUGE_PIN)
	pinMode(LOAD_GAUGE_PIN, arduino::OUTPUT);
#if defined(ESP32)
	analogWriteFrequency(2, 5000);
	analogWriteResolution(2, 8);
#endif
#endif

#if defined(ESP32)
	const esp_timer_create_args_t periodic_timer_args = {
		.callback = &periodic_timer_callback,
		.arg      = this,
		.name     = "kw11-l"
	};
	ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &kw11l_periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(kw11l_periodic_timer, 1000000 / int_frequency));
#elif defined(TEENSY4_1)
	timer = xTimerCreate("kw11-l", pdMS_TO_TICKS(1000 / int_frequency), pdTRUE, 123, periodic_timer_callback);
	xTimerStart(timer, 1);
#elif defined(BUILD_FOR_PICO2W)
	add_repeating_timer_us(-1000000 / int_frequency, periodic_timer_callback, this, &timer);
#elif defined(FREERTOS)
	xTaskCreate(&thread_wrapper_kw11, "kw11-l", 1536, this, 2, nullptr);
#else
	th = new std::thread(std::ref(*this));

#if !defined(_WIN32)
	int         policy { };
	sched_param param  { };
	pthread_getschedparam(th->native_handle(), &policy, &param);
	policy = SCHED_RR;
	param.sched_priority = sched_get_priority_max(policy);
	pthread_setschedparam(th->native_handle(), policy, &param);
#endif
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
	return int_frequency;
}

void kw11_l::tick()
{
	total_ticks++;

	cnsl->set_LED_state(false);

#if defined(LOAD_GAUGE_PIN)
	uint64_t instr_exec_count = b->getCpu()->get_instructions_executed_count();
	uint64_t count_last_interval = instr_exec_count < prev_instructions_executed ? 0 : instr_exec_count - prev_instructions_executed;
	// can become negative when a CPU reset is invoked
	max_instructions_count = std::max(max_instructions_count, count_last_interval);
//#if defined(TEENSY4_1)
	analogWrite(LOAD_GAUGE_PIN, count_last_interval * 255 / std::max(uint64_t(1), max_instructions_count));
//#elif defined(ESP32)
//#endif
	prev_instructions_executed = instr_exec_count;
#endif

	if (*cnsl->get_running_flag()) {
		do_interrupt();
#if defined(LOAD_GAUGE_PIN)
		last_instructions_count = count_last_interval;
#endif
	}
}

#if !defined(TEENSY4_1) && !defined(BUILD_FOR_PICO2W)
void kw11_l::operator()()
{
	set_thread_name("kek:kw-11l");

	DOLOG(log_ss::LS_GENERIC, "Starting KW11-L thread");

	timespec next { };
	clock_gettime(CLOCK_MONOTONIC, &next);

	while(!stop_flag) {
		int f = std::max(1, int(int_frequency));

		next.tv_nsec += 1000'000'000 / f;  // usually 50 or 60 Hz
		while (next.tv_nsec >= 1'000'000'000) {
			next.tv_nsec -= 1'000'000'000;
			next.tv_sec++;
		}

		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);

		tick();
	}

	DOLOG(log_ss::LS_GENERIC, "KW11-L thread terminating");
}
#endif

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
		DOLOG(log_ss::LS_GENERIC, "KW11-L read_word not for us (%06o)", a);
		return 0;
	}

	my_unique_lock lck(&lc_csr_lock);

	return lf_csr;
}

void kw11_l::set_interrupt_frequency(const int Hz)
{
	int_frequency = Hz;
#if defined(ESP32)
	ESP_ERROR_CHECK(esp_timer_stop(kw11l_periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(kw11l_periodic_timer, 1000000 / int_frequency));
#elif defined(TEENSY4_1)
	xTimerChangePeriod(timer, pdMS_TO_TICKS(1000 / int_frequency), pdMS_TO_TICKS(1));
#elif defined(BUILD_FOR_PICO2W)
	cancel_repeating_timer(&timer);
	add_repeating_timer_ms(1000 / int_frequency, periodic_timer_callback, this, &timer);
#endif
}

void kw11_l::write_byte(const uint16_t addr, const uint8_t value)
{
	if (addr != ADDR_LFC) {
		DOLOG(log_ss::LS_GENERIC, "KW11-L write_byte not for us (%06o to %06o)", value, addr);
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
		DOLOG(log_ss::LS_GENERIC, "KW11-L write_word not for us (%06o to %06o)", value, a);
		return;
	}

	my_unique_lock lck(&lc_csr_lock);

	DOLOG(log_ss::LS_GENERIC, "WRITE-I/O set line frequency clock/status register: %06o", value);
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

#if IS_POSIX
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
#endif
