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
