// (C) 2023 by Folkert van Heusden
// Released under MIT license

#include <SPI.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "console_shabadge.h"
#include "cpu.h"
#include "error.h"
#include "utils.h"


#define COLORED     0
#define UNCOLORED   1

console_shabadge::console_shabadge(std::atomic_uint32_t *const stop_event, std::vector<Stream *> & io_ports) :
	console_esp32(stop_event, io_ports, 296 / 8, 128 / 8)
{
	if (epd.Init() != 0)
		Serial.println("Init of DEPG0290B01 failed");
	else {
		Serial.println("DEPG0290B01 initialized");

		paint = new Paint(image, 0, 0);

		paint->SetRotate(ROTATE_270);
		paint->SetWidth(128);
		paint->SetHeight(296);
		paint->Clear(UNCOLORED);

		epd.ClearFrameMemory(UNCOLORED);
	}
}

console_shabadge::~console_shabadge()
{
	stop_thread();

	delete paint;
}

void console_shabadge::put_char_ll(const char c)
{
	console_esp32::put_char_ll(c);

	screen_updated_ts = millis();
	screen_updated    = true;
}

void console_shabadge::panel_update_thread()
{
	for(;;) {
		vTaskDelay(100 / portTICK_RATE_MS);

		if (screen_updated && millis() - screen_updated_ts >= 1000) {
			screen_updated = false;

			paint->Clear(UNCOLORED);

			for(int y=0; y<t_height; y++) {
				for(int x=0; x<t_width; x++) {
					char c = screen_buffer[y * t_width + x];

					if (c <= 0)
						c = ' ';

					paint->DrawCharAt(x * 8, y * 8, c, &Font8, COLORED);
				}
			}

			epd.SetFrameMemory(paint->GetImage(), 0, 0, paint->GetWidth(), paint->GetHeight());
			epd.DisplayFrame();
		}
	}
}
