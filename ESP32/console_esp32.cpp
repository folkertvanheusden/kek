// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <Adafruit_NeoPixel.h>
#include <stdio.h>
#include <unistd.h>

#include "bus.h"
#include "console_esp32.h"
#include "cpu.h"
#include "esp32.h"
#include "error.h"
#include "utils.h"


console_esp32::console_esp32(std::atomic_uint32_t *const stop_event, comm *const io_port, const int t_width, const int t_height) :
	console(stop_event, t_width, t_height),
	io_port(io_port)
{
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

void console_esp32::panel_update_thread()
{
	DOLOG(info, false, "panel task started");

	cpu *const c = b->getCpu();

#if !defined(BUILD_FOR_RP2040) && defined(NEOPIXELS_PIN)
	constexpr const uint8_t n_leds = 64;
#if defined(RGBW_PIXELS)
	Adafruit_NeoPixel pixels(n_leds, NEOPIXELS_PIN, NEO_RGBW);
#else
	Adafruit_NeoPixel pixels(n_leds, NEOPIXELS_PIN, NEO_RGB);
#endif
	pixels.begin();

	pixels.clear();
	pixels.show();

	constexpr uint8_t brightness = 16;
	const uint32_t magenta = pixels.Color(brightness, 0,          brightness);
	const uint32_t red     = pixels.Color(brightness, 0,          0);
	const uint32_t green   = pixels.Color(0,          brightness, 0);
	const uint32_t blue    = pixels.Color(0,          0,          brightness);
	const uint32_t yellow  = pixels.Color(brightness, brightness, 0);
	const uint32_t white   = pixels.Color(brightness, brightness, brightness, brightness);

	const uint32_t run_mode_led_color[4] = { red, yellow, blue, green };

	// initial animation
	for(uint8_t i=0; i<n_leds; i++) {
		pixels.setPixelColor(i, brightness, brightness, brightness);

		int p = i - 10;
		if (p < 0)
			p += n_leds;

		pixels.setPixelColor(p, 0, 0, 0);

		pixels.show();

		delay(10);
	}

	pixels.clear();
	pixels.show();

	while(!stop_panel) {
		vTaskDelay(100 / portTICK_PERIOD_MS);

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
				pixels.setPixelColor(pixel_offset++, disk_write_activity_flag ? blue  : 0);

				// 1
			}
			else {
				pixels.clear();

				pixels.setPixelColor(current_PC * n_leds / 65536, led_color);
			}

			pixels.show();
		}
		catch(std::exception & e) {
			put_string_lf(format("Exception in panel thread: %s", e.what()));
		}
		catch(int e) {
			put_string_lf(format("Exception in panel thread: %d", e));
		}
		catch(...) {
			put_string_lf("Unknown exception in panel thread");
		}
	}

	pixels.clear();
	pixels.show();
#elif defined(HEARTBEAT_PIN)
	uint64_t prev_count = 0;
	bool     led_state  = true;

	while(!stop_panel) {
		vTaskDelay(333 / portTICK_PERIOD_MS);

		uint64_t current_count = c->get_instructions_executed_count();
		if (prev_count != current_count) {
			prev_count = current_count;

			digitalWrite(HEARTBEAT_PIN, led_state ? HIGH : LOW);
			led_state = !led_state;
		}
	}
#endif

	DOLOG(info, false, "panel task terminating");
}
