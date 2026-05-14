// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32) || defined(BUILD_FOR_PICO2W)
#include <Arduino.h>
#include <LittleFS.h>
#elif defined(_WIN32)
#include <ws2tcpip.h>
#include <winsock2.h>
#else
#include <pwd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif
#if defined(ESP32)
#include <lwip/sockets.h>
#endif

#include <ArduinoJson.h>
#include <errno.h>
#include <optional>
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
#include <sys/socket.h>
#include "win32.h"
#endif


void setBit(uint16_t & v, const int bit, const bool vb)
{
	const uint16_t mask = 1 << bit;

	v &= ~mask;

	if (vb)
		v |= mask;
}

std::string format(const char *const fmt, ...)
{
#if defined(BUILD_FOR_PICO2W) || defined(ESP32)
	char buffer[384];
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
#if defined(ESP32) || defined(BUILD_FOR_PICO2W)
	return millis();
#else
	timeval tv { };
	// TODO replace gettimeofday by clock_gettime
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

uint64_t get_us()
{
#if defined(BUILD_FOR_PICO2W)
	return micros();
#else
	timeval tv { };
	// TODO replace gettimeofday by clock_gettime
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000l + tv.tv_usec;
#endif
}

int parity(int v)
{
	return __builtin_parity(v);
}

void myusleep(uint64_t us)
{
#if defined(ESP32) || defined(BUILD_FOR_PICO2W)
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

	req.tv_sec  = us / 1000000l;
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
#if !defined(ESP32) && !defined(BUILD_FOR_PICO2W)
	if (name.length() > 15)
		name = name.substr(0, 15);

	pthread_setname_np(pthread_self(), name.c_str());
#endif
}

std::string get_thread_name()
{
#if IS_POSIX || defined(_WIN32)
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

	while(len > 0) {
		ssize_t rc = write(fd, whereto, len);

		if (rc == -1)
			return -1;
		else if (rc == 0)
			return -1;
		else {
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

	while(len > 0) {
		ssize_t rc = read(fd, whereto, len);

		if (rc == -1)
			return -1;
		else if (rc == 0)
			break;
		else {
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

bool set_nodelay(const int fd)
{
        int flags = 1;
#if defined(__FreeBSD__) || defined(ESP32) || defined(_WIN32)
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&flags), sizeof(flags)) == -1)
		return false;
#elif !defined(BUILD_FOR_PICO2W)
        if (setsockopt(fd, SOL_TCP, TCP_NODELAY, reinterpret_cast<void *>(&flags), sizeof(flags)) == -1)
		return false;
#endif

	return true;
}

#if !defined(BUILD_FOR_PICO2W)
std::string get_endpoint_name(const int fd)
{
	sockaddr_in addr { };
	socklen_t   addr_len = sizeof addr;

	if (getpeername(fd, reinterpret_cast<sockaddr *>(&addr), &addr_len) == -1)
		return format("FAILED TO FIND NAME OF %d: %s", fd, strerror(errno));

	return std::string(inet_ntoa(addr.sin_addr)) + ":" + format("%d", ntohs(addr.sin_port));
}
#endif

std::optional<JsonDocument> deserialize_file(const std::string & filename)
{
	JsonDocument j;

#if defined(ESP32)
        File data_file = LittleFS.open(filename.c_str(), "r");
        if (!data_file)
		return { };

	deserializeJson(j, data_file);
	data_file.close();
#else
	FILE *fh = fopen(filename.c_str(), "r");
	if (!fh)
		return { };

	std::string j_in;
	char        buffer[4096];
	for(;;) {
		char *rc = fgets(buffer, sizeof buffer, fh);
		if (!rc)
			break;

		j_in += buffer;
	}

	fclose(fh);

	DeserializationError error = deserializeJson(j, j_in);
	if (error)
		return { };
#endif

	return j;
}

std::string file_in_user_home(const std::string & file)
{
#if defined(BUILD_FOR_PICO2W) || defined(ESP32)
	return "/" + file;
#else
	passwd *pw = getpwuid(getuid());
	if (!pw)
		return file;

	return std::string(pw->pw_dir) + "/" + file;
#endif
}

std::string get_configuration_string(const std::string & file, const std::string & default_value)
{
#if defined(ESP32)
	File data_file = LittleFS.open(file_in_user_home(file).c_str(), "r");
	if (!data_file)
		return default_value;

	auto rc = data_file.readString();
	data_file.close();

	return rc.c_str();
#else
	FILE *fh = fopen(file_in_user_home(file).c_str(), "r");
	if (!fh)
		return default_value;
	char buffer[64];
	fgets(buffer, sizeof buffer, fh);
	fclose(fh);

	return buffer;
#endif
}

uint32_t get_configuration_uint32(const std::string & file, const uint32_t default_value)
{
	uint8_t buffer[4] { };
#if defined(ESP32)
	File data_file = LittleFS.open(file_in_user_home(file).c_str(), "r");
	if (!data_file)
		return default_value;

	size_t size = data_file.size();
	if (size != 4) {
		data_file.close();
		return default_value;
	}

	data_file.read(buffer, 4);
	data_file.close();
#else
	FILE *fh = fopen(file_in_user_home(file).c_str(), "rb");
	if (!fh)
		return default_value;
	fread(buffer, 1, 4, fh);
	fclose(fh);
#endif

	return (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
}

bool put_configuration_uint32(const std::string & file, const uint32_t value)
{
	const uint8_t buffer[] = { uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value) };
#if defined(ESP32)
	File data_file = LittleFS.open(file_in_user_home(file).c_str(), "w");
	if (!data_file)
		return false;
	data_file.write(buffer, 4);
	data_file.close();
#else
	FILE *fh = fopen(file_in_user_home(file).c_str(), "wb");
	if (!fh)
		return false;
	fwrite(buffer, 1, 4, fh);
	fclose(fh);
#endif
	return true;
}

bool put_configuration_string(const std::string & file, const std::string & value)
{
#if defined(ESP32)
	File data_file = LittleFS.open(file_in_user_home(file).c_str(), "w");
	if (!data_file)
		return false;
	data_file.print(value.c_str());
	data_file.close();
#else
	FILE *fh = fopen(file_in_user_home(file).c_str(), "w");
	if (!fh)
		return false;
	fprintf(fh, "%s", value.c_str());
	fclose(fh);
#endif
	return true;
}
