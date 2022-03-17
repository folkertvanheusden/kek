// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
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
#endif

	m = new uint8_t[size]();
}

memory::~memory()
{
	delete [] m;
}

void memory::reset()
{
	memset(m, 0x00, size);
}
