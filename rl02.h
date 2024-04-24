// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "device.h"
#include "disk_backend.h"


#define RL02_CSR 0174400  // control status register
#define RL02_BAR 0174402  // bus address register
#define RL02_DAR 0174404  // disk address register
#define RL02_MPR 0174406  // multi purpose register
#define RL02_BASE  RL02_CSR
#define RL02_END  (RL02_MPR + 2)

constexpr const int rl02_sectors_per_track = 40;
constexpr const int rl02_track_count       = 512;
constexpr const int rl02_bytes_per_sector  = 256;

class bus;

class rl02 : public device
{
private:
	bus      *const b;
	uint16_t        registers[4];
	uint8_t         xfer_buffer[rl02_bytes_per_sector];
	int16_t         track  { 0 };
	uint8_t         head   { 0 };
	uint8_t         sector { 0 };
	uint16_t        mpr[3];
	std::vector<disk_backend *> fhs;

	std::atomic_bool *const disk_read_acitivity  { nullptr };
	std::atomic_bool *const disk_write_acitivity { nullptr };

	uint32_t get_bus_address() const;
	void     update_bus_address(const uint32_t a);
	void     update_dar();
	uint32_t calc_offset() const;

public:
	rl02(const std::vector<disk_backend *> & files, bus *const b, std::atomic_bool *const disk_read_acitivity, std::atomic_bool *const disk_write_acitivity);
	virtual ~rl02();

	void reset() override;

	uint8_t  readByte(const uint16_t addr) override;
	uint16_t readWord(const uint16_t addr) override;

	void writeByte(const uint16_t addr, const uint8_t  v) override;
	void writeWord(const uint16_t addr, const uint16_t v) override;
};
