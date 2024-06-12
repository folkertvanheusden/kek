// (C) 2024 by Folkert van Heusden
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


#define RP06_CR 0176700  // control register
#define RP06_BASE  RP06_CSR
#define RP06_END  (RP06_MPR + 2)


class bus;

class rp06: public disk_device
{
private:
	bus      *const b;

	uint16_t registers[20] { };

	std::atomic_bool *const disk_read_activity  { nullptr };
	std::atomic_bool *const disk_write_activity { nullptr };

public:
	rp06(bus *const b, std::atomic_bool *const disk_read_activity, std::atomic_bool *const disk_write_activity);
	virtual ~rp06();

	void begin() override;
	void reset() override;

	void show_state(console *const cnsl) const override;

	JsonDocument serialize() const;
	static rp06 *deserialize(const JsonVariantConst j, bus *const b);

	uint8_t  read_byte(const uint16_t addr) override;
	uint16_t read_word(const uint16_t addr) override;

	void write_byte(const uint16_t addr, const uint8_t  v) override;
	void write_word(const uint16_t addr, const uint16_t v) override;
};
