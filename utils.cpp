// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32) || defined(BUILD_FOR_RP2040)
#include <Arduino.h>
#include "rp2040.h"
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif
#include <sys/socket.h>

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <string>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <sys/time.h>

#if defined(_WIN32)
#include "win32.h"
#endif

#include "log.h"


void setBit(uint16_t & v, const int bit, const bool vb)
{
	const uint16_t mask = 1 << bit;

	v &= ~mask;

	if (vb)
		v |= mask;
}

std::string format(const char *const fmt, ...)
{
#if defined(BUILD_FOR_RP2040) || defined(ESP32)
	char buffer[256];
        va_list ap;

        va_start(ap, fmt);
	vsnprintf(buffer, sizeof buffer, fmt, ap);
	va_end(ap);

	return buffer;
#else
	char *buffer = nullptr;
        va_list ap;

        va_start(ap, fmt);
        (void)vasprintf(&buffer, fmt, ap);
        va_end(ap);

	std::string result = buffer;
	free(buffer);

	return result;
#endif
}

unsigned long get_ms()
{
#if defined(ESP32) || defined(BUILD_FOR_RP2040)
	return millis();
#else
	timeval tv;

	// TODO replace gettimeofday by clock_gettime
	gettimeofday(&tv, NULL);

	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

uint64_t get_us()
{
#if defined(ESP32) || defined(BUILD_FOR_RP2040)
	return micros();
#else
	timeval tv;

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
#if defined(ESP32) || defined(BUILD_FOR_RP2040)
	for(;;) {
		uint64_t n_ms = us / 1000;

		if (n_ms >= portTICK_PERIOD_MS) {
			vTaskDelay(n_ms / portTICK_PERIOD_MS);

			us -= n_ms * 1000;
		}
		else {
			delayMicroseconds(us);

			break;
		}
	}
#else
	timespec req;

	req.tv_sec = us / 1000000l;
	req.tv_nsec = (us % 1000000l) * 1000l;

	for(;;) {
		timespec rem { 0, 0 };

		int rc = nanosleep(&req, &rem);

		if (rc == 0 || (rc == -1 && errno != EINTR))
			break;

		memcpy(&req, &rem, sizeof(timespec));
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
#if !defined(ESP32) && !defined(BUILD_FOR_RP2040)
	if (name.length() > 15)
		name = name.substr(0, 15);

	pthread_setname_np(pthread_self(), name.c_str());
#endif
}

std::string get_thread_name()
{
#if IS_POSIX
	char buffer[16 + 1] { };
	pthread_getname_np(pthread_self(), buffer, sizeof buffer);

	return buffer;
#else
	return pcTaskGetName(xTaskGetCurrentTaskHandle());
#endif
}

ssize_t WRITE(int fd, const char *whereto, size_t len)
{
	ssize_t cnt=0;

	while(len > 0)
	{
		ssize_t rc = write(fd, whereto, len);

		if (rc == -1)
			return -1;
		else if (rc == 0)
			return -1;
		else
		{
			whereto += rc;
			len -= rc;
			cnt += rc;
		}
	}

	return cnt;
}

ssize_t READ(int fd, char *whereto, size_t len)
{
	ssize_t cnt=0;

	while(len > 0)
	{
		ssize_t rc = read(fd, whereto, len);

		if (rc == -1)
			return -1;
		else if (rc == 0)
			break;
		else
		{
			whereto += rc;
			len -= rc;
			cnt += rc;
		}
	}

	return cnt;
}

void update_word(uint16_t *const w, const bool msb, const uint8_t v)
{
	if (msb) {
		(*w) &= 0x00ff;
		(*w) |= v << 8;
	}
	else {
		(*w) &= 0xff00;
		(*w) |= v;
	}
}

void set_nodelay(const int fd)
{
        int flags = 1;
#if defined(__FreeBSD__) || defined(ESP32)
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) == -1)
#else
        if (setsockopt(fd, SOL_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) == -1)
#endif
                DOLOG(warning, true, "Cannot disable nagle algorithm");
}
