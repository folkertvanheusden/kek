// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#if defined(ESP32)
#include <Arduino.h>
#endif
#include <cstdlib>
#include <cstring>

#include "memory.h"

memory::memory(const uint32_t size): size(size)
{
#if defined(ESP32)
	Serial.print(F("Memory size (in bytes, decimal): "));
	Serial.println(size);

	if (psramFound()) {
		Serial.println(F("Using PSRAM"));

		m = reinterpret_cast<uint8_t *>(ps_malloc(size));

		reset();
	}
	else {
		m = reinterpret_cast<uint8_t *>(calloc(1, size));
	}
#else
	m = reinterpret_cast<uint8_t *>(calloc(1, size));
#endif
}

memory::~memory()
{
	free(m);
}

void memory::reset()
{
	memset(m, 0x00, size);
}

JsonDocument memory::serialize() const
{
	JsonDocument j;

	j["size"] = size;

	JsonArray ja;
	for(size_t i=0; i<size; i++)
		ja.add(m[i]);
	j["contents"] = ja;

	return j;
}

memory *memory::deserialize(const json_t *const j)
{
	size_t  size = j["size"];
	memory *m    = new memory(size);

	JsonArray ja = j["contents"];
	uint32_t  i  = 0;
	for(auto v: ja)
		m->m[i++] = v;

	return m;
}
