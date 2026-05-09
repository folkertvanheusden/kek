// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#pragma once

#include "gen.h"
#include <ArduinoJson.h>
#include <cstdint>
#include <endian.h>

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

	JsonDocument serialize() const;
	static memory *deserialize(const JsonVariantConst j);

	inline uint16_t read_byte(const uint32_t a) const { return m[a]; }
	inline void write_byte(const uint32_t a, const uint16_t v) { m[a] = v; }

#if __BYTE_ORDER == __LITTLE_ENDIAN
	inline uint16_t read_word(const uint32_t a) const { return *reinterpret_cast<uint16_t *>(&m[a]); }
	inline void write_word(const uint32_t a, const uint16_t v) { *reinterpret_cast<uint16_t *>(&m[a]) = v; }
#else
	inline uint16_t read_word(const uint32_t a) const { return m[a] | (m[a + 1] << 8); }
	inline void write_word(const uint32_t a, const uint16_t v) { m[a] = v; m[a + 1] = v >> 8; }
#endif
};
