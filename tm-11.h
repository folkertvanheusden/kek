// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>

#include "bus.h"
#include "device.h"

#define TM_11_MTS	0172520	// status register
#define TM_11_MTC	0172522	// command register
#define TM_11_MTBRC	0172524	// byte record counter
#define TM_11_MTCMA	0172526	// current memory address register
#define TM_11_MTD	0172530	// data buffer register
#define TM_11_MTRD	0172532	// TU10 read lines
#define TM_11_BASE	TM_11_MTS
#define TM_11_END	(TM_11_MTRD + 2)


class memory;

class tm_11 : public device
{
private:
	memory   *const m                  { nullptr };
	uint16_t        registers[6]       { 0       };
	uint8_t         xfer_buffer[65536];
	int             offset             { 0       };
	FILE           *fh                 { nullptr };
	std::string     tape_file;

public:
	tm_11(bus *const b);
	virtual ~tm_11();

	void load(const std::string & file);
	void unload();

	void reset() override;

	void show_state(console *const cnsl) const override;

	uint8_t  read_byte(const uint16_t addr) override;
	uint16_t read_word(const uint16_t addr) override;

	void write_byte(const uint16_t addr, const uint8_t v) override;
	void write_word(const uint16_t addr, uint16_t v)      override;
};
