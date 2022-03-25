// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#if defined(ESP32)
#include <Arduino.h>
#endif
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string>
#include <string.h>
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
#if defined(ESP32)
	return millis();
#else
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

int parity(int v)
{
	return __builtin_parity(v); // FIXME
}

void myusleep(uint64_t us)
{
	struct timespec req;

	req.tv_sec = us / 1000000l;
	req.tv_nsec = (us % 1000000l) * 1000l;

	for(;;) {
		struct timespec rem { 0, 0 };

		int rc = nanosleep(&req, &rem);
		if (rc == 0 || (rc == -1 && errno != EINTR))
			break;

		memcpy(&req, &rem, sizeof(struct timespec));
	}
}

