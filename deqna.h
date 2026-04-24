// (C) 2026 by Folkert van Heusden
// Released under MIT license

#pragma once

#include "bus.h"
#include "device.h"

#define DEQNA_BASE   0174440
#define DEQNA_VECTOR 0174454
#define DEQNA_CSR    0174456
#define DEQNA_END    (DEQNA_CSR + 2)

class deqna : public device
{
private:
	uint16_t registers  [8] { 0 };
	uint8_t  mac_address[6] { 0 };

public:
	deqna(bus *const b, const uint8_t mac_address[6]);
	virtual ~deqna();

	void reset() override;

	void show_state(console *const cnsl) const override;

	uint16_t read_word(const uint16_t addr) override;

	void write_byte(const uint16_t addr, const uint8_t v) override;
	void write_word(const uint16_t addr, uint16_t v)      override;
};
