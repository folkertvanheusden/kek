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
#include <vector>
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

	// TODO replace gettimeofday by clock_gettime
	gettimeofday(&tv, NULL);

	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

uint64_t get_us()
{
#if defined(ESP32)
	return micros();
#else
	struct timeval tv;

	// TODO replace gettimeofday by clock_gettime
	gettimeofday(&tv, NULL);

	return tv.tv_sec * 1000000l + tv.tv_usec;
#endif
}

int parity(int v)
{
	return __builtin_parity(v); // TODO
}

void myusleep(uint64_t us)
{
#if defined(ESP32)
	for(;;) {
		uint64_t n_ms = us / 1000;

		if (n_ms >= portTICK_RATE_MS) {
			vTaskDelay(n_ms / portTICK_RATE_MS);

			us -= n_ms * 1000;
		}
		else {
			delayMicroseconds(us);

			break;
		}
	}
#else
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
#endif
}

std::vector<std::string> split(std::string in, std::string splitter)
{
	std::vector<std::string> out;
	size_t splitter_size = splitter.size();

	for(;;)
	{
		size_t pos = in.find(splitter);
		if (pos == std::string::npos)
			break;

		std::string before = in.substr(0, pos);
		if (!before.empty())
			out.push_back(before);

		size_t bytes_left = in.size() - (pos + splitter_size);
		if (bytes_left == 0)
			return out;

		in = in.substr(pos + splitter_size);
	}

	if (in.size() > 0)
		out.push_back(in);

	return out;
}

void set_thread_name(std::string name)
{
#if !defined(ESP32)
	if (name.length() > 15)
		name = name.substr(0, 15);

	pthread_setname_np(pthread_self(), name.c_str());
#endif
}
