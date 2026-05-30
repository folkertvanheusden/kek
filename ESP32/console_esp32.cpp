// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#if defined(NEOPIXELS_PIN)
#include <Adafruit_NeoPixel.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "blinkenlights.h"
#include "bus.h"
#include "console_esp32.h"
#include "cpu.h"
#if defined(BUILD_FOR_PICO2W)
#include "pico2w.h"
#elif defined(TEENSY4_1)
#include "teensy4_1.h"
#else
#include "esp32.h"
#endif
#include "error.h"
#include "utils.h"


constexpr const uint8_t brightness = 16;

console_esp32::console_esp32(kek_event_t *const stop_event, comm *const io_port, const int t_width, const int t_height) :
	console_comm(stop_event, io_port, t_width, t_height)
{
#if defined(WAVESHARE_S3_ETH)
	rgb_led.begin();
	rgb_led.setBrightness(50);
#endif
}

console_esp32::~console_esp32()
{
	stop_thread();
}

void console_esp32::set_panel_mode(const panel_mode_t pm)
{
	panel_mode = pm;
}

int console_esp32::wait_for_char_ll(const short timeout)
{
	for(short i=0; i<timeout / 10; i++) {
		if (io_port->has_data())
			return io_port->get_byte();

		vTaskDelay(10 / portTICK_PERIOD_MS);
	}

	return -1;
}

void console_esp32::put_char_ll(const char c)
{
	io_port->send_data(reinterpret_cast<const uint8_t *>(&c), 1);
}

void console_esp32::put_string_lf(const std::string & what)
{
	put_string(what);
	put_string("\r\n");
}

void console_esp32::resize_terminal()
{
}

void console_esp32::refresh_virtual_terminal()
{
}

#if defined(NEOPIXELS_PIN)
void test_leds(Adafruit_NeoPixel & pixels, const int n_leds)
{
	// initial animation
	for(int i=0; i<n_leds; i++) {
		pixels.setPixelColor(i, brightness, brightness, brightness);
		int p = i - 10;
		if (p < 0)
			p += n_leds;
		pixels.setPixelColor(p, 0, 0, 0);
		pixels.show();

		delay(10);
	}
}
#endif

void console_esp32::panel_update_thread()
{
	DOLOG(log_ss::LS_COMM, "panel task started");
	cpu *const c = b->getCpu();

#if defined(NEOPIXELS_PIN)
	constexpr const uint8_t n_leds = 64;
#if defined(GRBW_PIXELS)
	Adafruit_NeoPixel pixels(n_leds, NEOPIXELS_PIN, NEO_GRBW + NEO_KHZ800);
#elif defined(RGBW_PIXELS)
	Adafruit_NeoPixel pixels(n_leds, NEOPIXELS_PIN, NEO_RGBW + NEO_KHZ800);
#else
	Adafruit_NeoPixel pixels(n_leds, NEOPIXELS_PIN, NEO_RGB  + NEO_KHZ800);
#endif
	pixels.begin();
	pixels.clear();
	pixels.show();

	const uint32_t magenta = pixels.Color(brightness, 0,          brightness);
	const uint32_t red     = pixels.Color(brightness, 0,          0);
	const uint32_t green   = pixels.Color(0,          brightness, 0);
	const uint32_t blue    = pixels.Color(0,          0,          brightness);
	const uint32_t yellow  = pixels.Color(brightness, brightness, 0);
	const uint32_t white   = pixels.Color(brightness, brightness, brightness, brightness);

	const uint32_t run_mode_led_color[4] = { red, yellow, blue, green };

#if defined(NEOPIXELS_PIN)
	test_leds(pixels, n_leds);
#endif

	pixels.clear();
	pixels.show();

	while(!stop_panel) {
		vTaskDelay(1000 / (portTICK_PERIOD_MS * refreshrate));

		if (do_test_panel) {
			do_test_panel = false;
			if (p_blinkenlights)
				p_blinkenlights->test();
#if defined(NEOPIXELS_PIN)
			test_leds(pixels, n_leds);
#endif
		}

		if (p_blinkenlights)
			p_blinkenlights->push(b, running_flag);

		try {
			// note that these are approximately as there's no mutex on the emulation
			uint16_t current_PSW   = c->getPSW();
			int      run_mode      = current_PSW >> 14;
			uint32_t led_color     = run_mode_led_color[run_mode];

			uint16_t current_PC    = c->getPC();

			if (panel_mode == PM_BITS) {
				memory_addresses_t rc  = b->getMMU()->calculate_physical_address(run_mode, current_PC);

				auto current_instr = b->peek_word(run_mode, current_PC);

				int pixel_offset = 0;

				for(uint8_t b=0; b<22; b++)
					pixels.setPixelColor(pixel_offset++, rc.physical_instruction & (1 << b) ? led_color : 0);

				for(uint8_t b=0; b<3; b++)
					pixels.setPixelColor(pixel_offset++, rc.apf ? yellow : 0);

				pixels.setPixelColor(pixel_offset++, rc.physical_instruction_is_psw | rc.physical_data_is_psw ? blue : 0);

				pixels.setPixelColor(pixel_offset++, b->getMMU()->is_enabled() ? white : 0);

				pixels.setPixelColor(pixel_offset++, b->getMMU()->getMMR3() & 7 ? white : 0);

				for(uint8_t b=0; b<16; b++)
					pixels.setPixelColor(pixel_offset++, current_PSW   & (1l << b) ? magenta : 0);

				if (current_instr.has_value()) {
					for(uint8_t b=0; b<16; b++)
						pixels.setPixelColor(pixel_offset++, current_instr.value() & (1l << b) ? red     : 0);
				}
				else {
					for(uint8_t b=0; b<16; b++)
						pixels.setPixelColor(pixel_offset++, 0);
				}

				pixels.setPixelColor(pixel_offset++, running_flag             ? white : 0);

				pixels.setPixelColor(pixel_offset++, disk_read_activity_flag  ? blue  : 0);
				disk_read_activity_flag  = false;
				pixels.setPixelColor(pixel_offset++, disk_write_activity_flag ? blue  : 0);
				disk_write_activity_flag = false;

				pixels.setPixelColor(pixel_offset++, network_activity_flag    ? yellow: 0);
				network_activity_flag    = false;
			}
			else {
				pixels.clear();

				pixels.setPixelColor(current_PC * n_leds / 65536, led_color);
			}

			pixels.show();
		}
		catch(const std::exception & e) {
			put_string_lf(format("Exception in panel thread: %s", e.what()));
		}
		catch(const int e) {
			put_string_lf(format("Exception in panel thread: %d", e));
		}
		catch(...) {
			put_string_lf("Unknown exception in panel thread");
		}
	}

	pixels.clear();
	pixels.show();
#endif

	DOLOG(log_ss::LS_COMM, "panel task terminating");
}

void console_esp32::set_LED_state(const bool state)
{
	led_pulses = state;
#if 0
#if defined(WAVESHARE_S3_ETH)
	uint8_t intensity = state ? 255 : 0;
	rgb_led.setPixelColor(0, intensity, intensity, intensity);
	rgb_led.show();
#elif !defined(BUILD_FOR_PICO2W)
	my_unique_lock lck(&led_lock);
	digitalWrite(HEARTBEAT_PIN, state);
#endif
#endif
}

void console_esp32::pulse_LED()
{
#if 0
#if defined(WAVESHARE_S3_ETH)
	if (led_pulses <= 5)
		led_pulses++;
	uint8_t intensity = 255 - (led_pulses - 1) * 50;
	rgb_led.setPixelColor(0, 255, intensity, intensity);
	rgb_led.show();
#endif
#endif
}
