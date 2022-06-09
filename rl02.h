// (C) 2022 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#if defined(ESP32)
#include "esp32.h"
#endif

#define RL02_CSR 0174400  // control status register
#define RL02_BAR 0174402  // bus address register
#define RL02_DAR 0174404  // disk address register
#define RL02_MPR 0174406  // multi purpose register
#define RL02_BASE  RL02_CSR
#define RL02_END  (RL02_MPR + 2)

class bus;

class rl02
{
private:
	bus *const b;
	uint16_t registers[7];
	uint8_t xfer_buffer[512];
#if defined(ESP32)
	std::vector<File32 *> fhs;
#else
	std::vector<FILE *> fhs;
#endif
	std::atomic_bool *const disk_read_acitivity  { nullptr };
	std::atomic_bool *const disk_write_acitivity { nullptr };

public:
	rl02(const std::vector<std::string> & files, bus *const b, std::atomic_bool *const disk_read_acitivity, std::atomic_bool *const disk_write_acitivity);
	virtual ~rl02();

	uint8_t  readByte(const uint16_t addr);
	uint16_t readWord(const uint16_t addr);

	void writeByte(const uint16_t addr, const uint8_t  v);
	void writeWord(const uint16_t addr, const uint16_t v);
};
