#pragma once

#include "console.h"

class device
{
public:
	device() {
	}

	virtual ~device() {
	}

	virtual void reset() = 0;

	virtual void show_state(console *const cnsl) const = 0;

        virtual uint8_t  read_byte(const uint16_t addr);
        virtual uint16_t read_word(const uint16_t addr) = 0;

        virtual void write_byte(const uint16_t addr, const uint8_t  v) = 0;
        virtual void write_word(const uint16_t addr, const uint16_t v) = 0;
};
