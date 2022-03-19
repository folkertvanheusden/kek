// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>

#define PDP11TTY_TKS		0177560	// reader status
#define PDP11TTY_TKB		0177562	// reader buffer
#define PDP11TTY_TPS		0177564	// puncher status
#define PDP11TTY_TPB		0177566	// puncher buffer
#define PDP11TTY_BASE	PDP11TTY_TKS
#define PDP11TTY_END	(PDP11TTY_TPB + 2)

class memory;

class tty
{
private:
	uint16_t registers[4];
	bool testMode { false }, withUI { false };
	char c { 0 };

#if defined(ESP32)
	QueueHandle_t queue { nullptr };
#endif

public:
	tty(const bool withUI);
	virtual ~tty();

	void setTest() { testMode = true; }

#if defined(ESP32)
	QueueHandle_t & getTerminalQueue() { return queue; }
#endif

	void sendChar(const char v);

	uint8_t readByte(const uint16_t addr);
	uint16_t readWord(const uint16_t addr);

	void writeByte(const uint16_t addr, const uint8_t v);
	void writeWord(const uint16_t addr, uint16_t v);
};
