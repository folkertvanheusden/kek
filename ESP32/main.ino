// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <string.h>
#include <unistd.h>

#include "memory.h"
#include "cpu.h"
#include "tty.h"
#include "utils.h"
#include "error.h"


bus *b = nullptr;
cpu *c = nullptr;
tty *tty_ = nullptr;

uint16_t exec_addr = 0;

uint32_t start_ts = 0;

void setBootLoader(bus *const b)
{
	cpu *const c = b->getCpu();

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
void setup() {
	Serial.begin(115200);

	Serial.println(F("This PDP-11 emulator is called \"kek\" (reason for that is forgotten) and was written by Folkert van Heusden."));

	Serial.print(F("Size of int: "));
	Serial.println(sizeof(int));

	Serial.print(F("CPU clock frequency: "));
	Serial.println(getCpuFrequencyMhz());

	Serial.print(F("Free RAM before init: "));
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
	b->add_rk05(new rk05("xxdp+.rk", b));
	setBootLoader(b);

	Serial.print(F("Free RAM after init: "));
	Serial.println(ESP.getFreeHeap());

	pinMode(LED_BUILTIN, OUTPUT);

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

void loop() {
	icount++;

	if ((icount & 1023) == 0) {
		if (Serial.available()) {
			char c = Serial.read();

			if (c > 0 && c < 127)
				tty_->sendChar(c);
		}
	}

	if (c->step()) {
		Serial.println(F(""));
		Serial.println(F(" *** EMULATION STOPPED *** "));
		Serial.print(F("Instructions per second: "));
		Serial.println(icount * 1000.0 / (millis() - start_ts));
		delay(3000);
		Serial.println(F(" *** EMULATION RESTARTING *** "));

		c->setRegister(7, exec_addr);
		c->resetHalt();

		start_ts = millis();
		icount = 0;
	}
}
