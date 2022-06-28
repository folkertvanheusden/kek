// (C) 2018-2022 by Folkert van Heusden
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


#define RK05_DS		0777400	// drive status
#define RK05_ERROR	0777402 // error
#define RK05_CS		0777404 // control status
#define RK05_WC		0777406 // word count
#define RK05_BA		0777410 // bus address
#define RK05_DA		0777412 // disk address
#define RK05_DATABUF	0777414 // data buffer
#define RK05_BASE	RK05_DS
#define RK05_END	(RK05_DATABUF + 2)

class bus;

class rk05
{
private:
	bus *const b;
	uint16_t registers[7];
	uint8_t  xfer_buffer[512];
#if defined(ESP32)
	std::vector<File32 *> fhs;
#else
	std::vector<FILE *> fhs;
#endif
	std::atomic_bool *const disk_read_acitivity  { nullptr };
	std::atomic_bool *const disk_write_acitivity { nullptr };

public:
	rk05(const std::vector<std::string> & files, bus *const b, std::atomic_bool *const disk_read_acitivity, std::atomic_bool *const disk_write_acitivity);
	virtual ~rk05();

	uint8_t  readByte(const uint32_t addr);
	uint16_t readWord(const uint32_t addr);

	void writeByte(const uint32_t addr, const uint8_t v);
	void writeWord(const uint32_t addr, uint16_t      v);
};
