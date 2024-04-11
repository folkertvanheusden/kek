// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <atomic>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>

#include "bus.h"
#include "console.h"


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
	console *const c      { nullptr };
	bus     *const b      { nullptr };

#if defined(BUILD_FOR_RP2040)
	SemaphoreHandle_t chars_lock { xSemaphoreCreateBinary() };
#else
	std::mutex        chars_lock;
#endif
	std::vector<char> chars;

	uint16_t registers[4] { 0 };

#if !defined(BUILD_FOR_RP2040)
	std::thread     *th        { nullptr };
#endif
	std::atomic_bool stop_flag { false };

	void notify_rx();

public:
	tty(console *const c, bus *const b);
	virtual ~tty();

	void reset();

	uint8_t readByte(const uint16_t addr);
	uint16_t readWord(const uint16_t addr);

	void writeByte(const uint16_t addr, const uint8_t v);
	void writeWord(const uint16_t addr, uint16_t v);

	void operator()();
};
