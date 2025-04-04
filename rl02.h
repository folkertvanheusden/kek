// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include "gen.h"
#include <ArduinoJson.h>
#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "disk_device.h"
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

class rl02: public disk_device
{
private:
	bus      *const b;
	uint16_t        registers[4];
	uint8_t         xfer_buffer[rl02_bytes_per_sector];
	int16_t         track  { 0 };
	uint8_t         head   { 0 };
	uint8_t         sector { 0 };
	uint16_t        mpr[3];

	std::atomic_bool *const disk_read_activity  { nullptr };
	std::atomic_bool *const disk_write_activity { nullptr };

	uint32_t get_bus_address() const;
	void     update_bus_address(const uint32_t a);
	void     update_dar();
	uint32_t calc_offset() const;

public:
	rl02(bus *const b, std::atomic_bool *const disk_read_activity, std::atomic_bool *const disk_write_activity);
	virtual ~rl02();

	void begin() override;
	void reset() override;

	void show_state(console *const cnsl) const override;

	JsonDocument serialize() const;
	static rl02 *deserialize(const JsonVariantConst j, bus *const b);

	uint8_t  read_byte(const uint16_t addr) override;
	uint16_t read_word(const uint16_t addr) override;

	void write_byte(const uint16_t addr, const uint8_t  v) override;
	void write_word(const uint16_t addr, const uint16_t v) override;
};
