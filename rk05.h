// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "disk_backend.h"


#define RK05_DS		0177400	// drive status
#define RK05_ERROR	0177402 // error
#define RK05_CS		0177404 // control status
#define RK05_WC		0177406 // word count
#define RK05_BA		0177410 // bus address
#define RK05_DA		0177412 // disk address
#define RK05_DATABUF	0177414 // data buffer
#define RK05_BASE	RK05_DS
#define RK05_END	(RK05_DATABUF + 2)

class bus;

class rk05
{
private:
	bus      *const b                { nullptr };
	uint16_t        registers  [7]   { 0       };
	std::vector<disk_backend *> fhs;
	uint8_t         xfer_buffer[512] { 0       };

	std::atomic_bool *const disk_read_acitivity  { nullptr };
	std::atomic_bool *const disk_write_acitivity { nullptr };

	uint32_t get_bus_address() const;
	void     update_bus_address(const uint16_t v);

public:
	rk05(const std::vector<disk_backend *> & files, bus *const b, std::atomic_bool *const disk_read_acitivity, std::atomic_bool *const disk_write_acitivity);
	virtual ~rk05();

	void reset();

	uint8_t  readByte(const uint16_t addr);
	uint16_t readWord(const uint16_t addr);

	void writeByte(const uint16_t addr, const uint8_t v);
	void writeWord(const uint16_t addr, uint16_t v);
};
