// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <FastLED.h>
#include <string.h>
#include <unistd.h>

#include "memory.h"
#include "cpu.h"
#include "tty.h"
#include "utils.h"
#include "error.h"


#define NEOPIXELS_PIN	27
bus *b    = nullptr;
cpu *c    = nullptr;
tty *tty_ = nullptr;

uint16_t exec_addr = 0;

uint32_t start_ts  = 0;

void setBootLoader(bus *const b) {
	cpu     *const c      = b->getCpu();

	const uint16_t offset = 01000;

	constexpr uint16_t bootrom[] = {
		0012700,
		0177406,
		0012710,
		0177400,
		0012740,
		0000005,
		0105710,
		0100376,
		0005007
	};

	for(size_t i=0; i<sizeof bootrom / 2; i++)
		b->writeWord(offset + i * 2, bootrom[i]);

	c->setRegister(7, offset);
}

void panel(void *p) {
	bus *const b = reinterpret_cast<bus *>(p);
	cpu *const c = b->getCpu();

	CRGB leds[1];  // FIXME 1: aantal leds, zie ook v
	FastLED.addLeds<NEOPIXEL, NEOPIXELS_PIN>(leds, 1);

	const CRGB run_mode_led_color[4] = { CRGB::Red, CRGB::Yellow, CRGB::Blue, CRGB::Green };

	for(;;) {
		vTaskDelay(100 / portTICK_RATE_MS);

		uint16_t current_PC  = c->getPC();
		uint16_t current_PSW = c->getPSW();

		CRGB     led_color   = run_mode_led_color[current_PSW >> 14];

		leds[0] = current_PC & (1 << 4) ? led_color : CRGB::Black;

		FastLED.show();
	}
}

void setup() {
	Serial.begin(115200);

	Serial.println(F("This PDP-11 emulator is called \"kek\" (reason for that is forgotten) and was written by Folkert van Heusden."));

	Serial.print(F("Size of int: "));
	Serial.println(sizeof(int));

	Serial.print(F("CPU clock frequency (MHz): "));
	Serial.println(getCpuFrequencyMhz());

	Serial.print(F("Free RAM before init (decimal bytes): "));
	Serial.println(ESP.getFreeHeap());

	Serial.println(F("Init bus"));
	b = new bus();

	Serial.println(F("Init CPU"));
	c = new cpu(b);

	Serial.println(F("Connect CPU to BUS"));
	b->add_cpu(c);

	c->setEmulateMFPT(true);

	Serial.println(F("Init TTY"));
	tty_ = new tty(false);
	Serial.println(F("Connect TTY to bus"));
	b->add_tty(tty_);

	Serial.println(F("Load RK05"));
	b->add_rk05(new rk05("", b));
	setBootLoader(b);

	Serial.print(F("Free RAM after init: "));
	Serial.println(ESP.getFreeHeap());

	pinMode(LED_BUILTIN, OUTPUT);

	Serial.flush();

	Serial.print(F("Starting panel (on CPU 0, main emulator runs on CPU "));
	Serial.print(xPortGetCoreID());
	Serial.println(F(")"));
	xTaskCreatePinnedToCore(&panel, "panel", 2048, b, 5, nullptr, 0);

	Serial.println(F("Press <enter> to start"));

	for(;;) {
		if (Serial.available()) {
			int c = Serial.read();
			if (c == 13 || c == 10)
				break;
		}

		delay(1);
	}

	Serial.println(F("Emulation starting!"));

	start_ts = millis();
}

uint32_t icount = 0;

void dump_state(bus *const b) {
	cpu *const c = b->getCpu();

	uint32_t now = millis();
	uint32_t t_diff = now - start_ts;

	double mips = icount / (1000.0 * t_diff);

	// see https://retrocomputing.stackexchange.com/questions/6960/what-was-the-clock-speed-and-ips-for-the-original-pdp-11
	constexpr double pdp11_clock_cycle = 150;  // ns, for the 11/70
	constexpr double pdp11_mhz = 1000.0 / pdp11_clock_cycle; 
	constexpr double pdp11_avg_cycles_per_instruction = (1 + 5) / 2.0;
	constexpr double pdp11_estimated_mips = pdp11_mhz / pdp11_avg_cycles_per_instruction;

	Serial.print(F("MIPS: "));
	Serial.println(mips);

	Serial.print(F("emulation speed (aproximately): "));
	Serial.print(mips * 100 / pdp11_estimated_mips);
	Serial.println('%');

	Serial.print(F("PC: "));
	Serial.println(c->getPC());

	Serial.print(F("Uptime (ms): "));
	Serial.println(t_diff);
}

void loop() {
	icount++;

	if ((icount & 1023) == 0) {
		if (Serial.available()) {
			char c = Serial.read();

			if (c == 5)
				dump_state(b);
			else if (c > 0 && c < 127)
				tty_->sendChar(c);
		}
	}

	if (c->step()) {
		Serial.println(F(""));
		Serial.println(F(" *** EMULATION STOPPED *** "));
		dump_state(b);
		delay(3000);
		Serial.println(F(" *** EMULATION RESTARTING *** "));

		c->setRegister(7, exec_addr);
		c->resetHalt();

		start_ts = millis();
		icount = 0;
	}
}
