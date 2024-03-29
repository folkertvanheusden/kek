// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <assert.h>
#include <stdint.h>

class memory
{
private:
	const uint32_t size { 0       };
	uint8_t       *m    { nullptr };

public:
	memory(const uint32_t size);
	~memory();

	void reset();

	uint16_t readByte(const uint32_t a) const { return m[a]; }
	void writeByte(const uint32_t a, const uint16_t v) { assert(a < size); m[a] = v; }

	uint16_t readWord(const uint32_t a) const { return m[a] | (m[a + 1] << 8); }
	void writeWord(const uint32_t a, const uint16_t v) { assert(a < size - 1); m[a] = v; m[a + 1] = v >> 8; }
};
