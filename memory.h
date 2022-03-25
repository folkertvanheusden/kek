// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <stdint.h>

class memory
{
private:
	const uint32_t size;
	uint8_t *m { nullptr };

public:
	memory(const uint32_t size);
	~memory();

	void reset();

	uint16_t readByte(const uint32_t a) const { return m[a]; }
	void writeByte(const uint32_t a, const uint16_t v) { m[a] = v; }

	uint16_t readWord(const uint32_t a) const { return m[a] | (m[a + 1] << 8); }
	void writeWord(const uint32_t a, const uint16_t v) { m[a] = v; m[a + 1] = v >> 8; }
};
