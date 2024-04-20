// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#if defined(ESP32)
#include <Arduino.h>
#endif
#include <string.h>

#include "memory.h"

memory::memory(const uint32_t size) : size(size)
{
#if defined(ESP32)
	Serial.print(F("Memory size (in bytes, decimal): "));
	Serial.println(size);

	if (size > 12 * 8192) {
		Serial.println(F("Using PSRAM"));
		is_psram = true;

		m = reinterpret_cast<uint8_t *>(ps_malloc(size));

		reset();
	}
	else {
		m = new uint8_t[size]();
	}
#else
	m = new uint8_t[size]();
#endif
}

memory::~memory()
{
#if defined(ESP32)
	if (is_psram)
		free(m);
	else
		delete [] m;
#else
	delete [] m;
#endif
}

void memory::reset()
{
	memset(m, 0x00, size);
}
