// (C) 2018 by Folkert van Heusden
// Released under AGPL v3.0
#include <string.h>

#include "memory.h"

memory::memory(const uint32_t size) : size(size)
{
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
