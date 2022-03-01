// (C) 2018 by Folkert van Heusden
// Released under AGPL v3.0
#include <stdarg.h>
#include <stdint.h>
#include <string>
#include <sys/time.h>

void setBit(uint16_t & v, const int bit, const bool vb)
{
	const uint16_t mask = 1 << bit;

	v &= ~mask;

	if (vb)
		v |= mask;
}

std::string format(const char *const fmt, ...)
{
	char *buffer = nullptr;
        va_list ap;

        va_start(ap, fmt);
        (void)vasprintf(&buffer, fmt, ap);
        va_end(ap);

	std::string result = buffer;
	free(buffer);

	return result;
}

unsigned long get_ms()
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int parity(int v)
{
	return __builtin_parity(v); // FIXME
}
