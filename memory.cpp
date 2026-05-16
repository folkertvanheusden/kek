// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#if defined(ESP32)
#include <Arduino.h>
#endif
#include <cstdlib>
#include <cstring>

#include "log.h"
#include "memory.h"

#if defined(TEENSY4_1)
extern "C" uint8_t external_psram_size;
#endif

memory::memory(const uint32_t size): size(size)
{
#if defined(ESP32)
	DOLOG(info, false, "Memory size (in bytes, decimal): %d", size);

	if (psramFound()) {
		DOLOG(info, false, "Using PSRAM");
		m = reinterpret_cast<uint8_t *>(ps_malloc(size));
		reset(true);
	}
	else {
		m = reinterpret_cast<uint8_t *>(calloc(1, size));
	}
#elif defined(TEENSY4_1)
	if (external_psram_size >= size / 1024 / 1024) {
		m = reinterpret_cast<uint8_t *>(extmem_malloc(size));
		reset(true);
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
#if defined(TEENSY4_1)
	if (external_psram_size >= size / 1024 / 1024)
		extmem_free(m);
	else
		free(m);
#else
	free(m);
#endif
}

void memory::reset(const bool hard)
{
	if (hard)
		memset(m, 0x00, size);
}

JsonDocument memory::serialize() const
{
	JsonDocument j;

	j["size"] = size;

	JsonDocument ja;
	JsonArray ja_work = ja.to<JsonArray>();
	for(size_t i=0; i<size; i++)
		ja_work.add(m[i]);
	j["contents"] = ja;

	return j;
}

memory *memory::deserialize(const JsonVariantConst j)
{
	size_t  size = j["size"];
	memory *m    = new memory(size);

	JsonArrayConst ja = j["contents"].as<JsonArrayConst>();
	uint32_t  i  = 0;
	for(auto v: ja)
		m->m[i++] = v;

	return m;
}
