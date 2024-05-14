// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once
#include <ArduinoJson.h>
#include <cstdint>

#include "gen.h"


class memory
{
private:
	const uint32_t size     { 0       };
	uint8_t       *m        { nullptr };

public:
	memory(const uint32_t size);
	~memory();

	uint32_t get_memory_size() const { return size; }

	void reset();

	JsonVariant serialize() const;
	static memory *deserialize(const JsonVariantConst j);

	uint16_t read_byte(const uint32_t a) const { return m[a]; }
	void write_byte(const uint32_t a, const uint16_t v) { if (a < size) m[a] = v; }

	uint16_t read_word(const uint32_t a) const { return m[a] | (m[a + 1] << 8); }
	void write_word(const uint32_t a, const uint16_t v) { if(a < size - 1) { m[a] = v; m[a + 1] = v >> 8; } }
};
