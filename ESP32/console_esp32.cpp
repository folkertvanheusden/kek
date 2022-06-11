#include <Adafruit_NeoPixel.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "console_esp32.h"
#include "cpu.h"
#include "error.h"


#define NEOPIXELS_PIN	25

console_esp32::console_esp32(std::atomic_uint32_t *const stop_event, bus *const b) :
	console(stop_event, b)
{
}

console_esp32::~console_esp32()
{
	stop_thread();
}

int console_esp32::wait_for_char_ll(const short timeout)
{
	for(short i=0; i<timeout / 10; i++) {
		if (Serial.available())
			return Serial.read();

		delay(10);
	}

	return -1;
}

void console_esp32::put_char_ll(const char c)
{
	Serial.print(c);
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
	Serial.println(F("panel task started"));

	cpu *const c = b->getCpu();

	constexpr const uint8_t n_leds = 60;
	Adafruit_NeoPixel pixels(n_leds, NEOPIXELS_PIN, NEO_RGBW);
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

	for(;;) {
		vTaskDelay(20 / portTICK_RATE_MS);

		// note that these are approximately as there's no mutex on the emulation
		uint16_t current_PSW   = c->getPSW();
		int      run_mode      = current_PSW >> 14;

		uint16_t current_PC    = c->getPC();
		uint32_t full_addr     = b->calculate_physical_address(run_mode, current_PC, false, false, true);

		uint16_t current_instr = b->readWord(current_PC);

		uint32_t led_color     = run_mode_led_color[run_mode];

		for(uint8_t b=0; b<22; b++)
			pixels.setPixelColor(b, full_addr & (1 << b) ? led_color : 0);

		for(uint8_t b=0; b<16; b++)
			pixels.setPixelColor(b + 22, current_PSW & (1 << b) ? magenta : 0);

		for(uint8_t b=0; b<16; b++)
			pixels.setPixelColor(b + 38, current_instr & (1 << b) ? red : 0);

		pixels.setPixelColor(54, running_flag             ? white : 0);

		pixels.setPixelColor(55, disk_read_activity_flag  ? blue  : 0);
		pixels.setPixelColor(56, disk_write_activity_flag ? blue  : 0);

		pixels.show();
	}
}
