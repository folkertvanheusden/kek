// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32) || defined(BUILD_FOR_PICO2W)
#include <Arduino.h>
#include <LittleFS.h>
#elif defined(_WIN32)
#include <ws2tcpip.h>
#include <winsock2.h>
#elif defined(TEENSY4_1)
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

#if !defined(_WIN32)
#include <ArduinoJson.h>
#endif
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
	char buf[64];

	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	if (n < (int)sizeof buf)
		return std::string(buf, n);

	std::string result(n, '\0');
	va_start(ap, fmt);
	vsnprintf(result.data(), n + 1, fmt, ap);
	va_end(ap);

	return result;
}

uint64_t get_ms()
{
#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
	return millis();
#else
	timespec tp { };
	// assuming 1ms resolution (true on linux)
#if defined(__APPLE__)
	clock_gettime(CLOCK_REALTIME, &tp);
#else
	clock_gettime(CLOCK_REALTIME_COARSE, &tp);
#endif
	return tp.tv_sec * 1000 + tp.tv_nsec / 1000000;
#endif
}

uint64_t get_us()
{
#if defined(ESP32)
	return esp_timer_get_time();
#elif defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
	return micros();
#else
	timespec tp { };
	clock_gettime(CLOCK_REALTIME, &tp);
	return tp.tv_sec * 1000000l + tp.tv_nsec / 1000;
#endif
}

int parity(int v)
{
	return __builtin_parity(v);
}

void myusleep(uint64_t us)
{
#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
	uint64_t end = get_us() + us;
	for(;;) {
		uint64_t now = get_us();
		if (now >= end)
			break;
		uint64_t n_ms = (end - now) / 1000;
		if (n_ms >= portTICK_PERIOD_MS)
			vTaskDelay(n_ms / portTICK_PERIOD_MS);
		else {
#if defined(FREERTOS)
			taskYIELD();
#else
			yield();
#endif
		}
	}
#elif defined(__APPLE__) || defined(_WIN32)
        timespec end { };
	us *= 1000;
	end.tv_nsec = us % 1000'000'000;
	end.tv_sec  = us / 1000'000'000;
	if (nanosleep(&end, nullptr) == -1)  // hope for the best
		printf("%s\n", strerror(errno));
#else
        timespec end { };
        clock_gettime(CLOCK_MONOTONIC, &end);
	us *= 1000;
	end.tv_nsec += us % 1000'000'000;
	end.tv_sec  += us / 1000'000'000;
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &end, nullptr);
#endif
}

std::vector<std::string> split(std::string in, const std::string & splitter)
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
#if !defined(ESP32) && !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
	if (name.length() > 15)
		name = name.substr(0, 15);

#if defined(__APPLE__)
	pthread_setname_np(name.c_str());
#else
	pthread_setname_np(pthread_self(), name.c_str());
#endif
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
#if defined(__FreeBSD__) || defined(ESP32) || defined(_WIN32) || defined(__APPLE__)
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&flags), sizeof(flags)) == -1)
		return false;
#elif !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
        if (setsockopt(fd, SOL_TCP, TCP_NODELAY, reinterpret_cast<void *>(&flags), sizeof(flags)) == -1)
		return false;
#endif

	return true;
}

#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
std::string get_endpoint_name(const int fd)
{
	sockaddr_in addr { };
	socklen_t   addr_len = sizeof addr;

	if (getpeername(fd, reinterpret_cast<sockaddr *>(&addr), &addr_len) == -1)
		return format("FAILED TO FIND NAME OF %d: %s", fd, strerror(errno));

	return std::string(inet_ntoa(addr.sin_addr)) + ":" + format("%d", ntohs(addr.sin_port));
}
#endif

#if !defined(_WIN32)
std::optional<JsonDocument> deserialize_file(const std::string & filename)
{
#if defined(ESP32)
        File data_file = LittleFS.open(filename.c_str(), "r");
        if (!data_file)
		return { };

	JsonDocument j;
	deserializeJson(j, data_file);
	data_file.close();
	return j;
#elif defined(TEENSY4_1)
	return { };
#else
	FILE *fh = fopen(filename.c_str(), "r");
	if (!fh)
		return { };

	bool ok = true;
	if (fseek(fh, 0, SEEK_END) != 0)
		ok = false;
	long size = ftell(fh);
	if (size < 0)
		ok = false;
	if (fseek(fh, 0, SEEK_SET) != 0)
		ok = false;
	std::vector<char> j_in(size + 1);
	JsonDocument j;
	if (fread(j_in.data(), 1, size, fh) < size_t(size))
		ok = false;
	fclose(fh);

	if (!ok)
		return { };

	DeserializationError error = deserializeJson(j, reinterpret_cast<const char *>(j_in.data()));
	if (error) {
		printf("DeserializationError %s\n", error.c_str());
		return { };
	}
	return j;
#endif
}
#endif

std::string file_in_user_home(const std::string & file)
{
#if defined(BUILD_FOR_PICO2W) || defined(ESP32) || defined(_WIN32) || defined(TEENSY4_1)
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
#elif defined(TEENSY4_1)
	return "";
#else
	FILE *fh = fopen(file_in_user_home(file).c_str(), "r");
	if (!fh)
		return default_value;
	char buffer[64];
	(void)fgets(buffer, sizeof buffer, fh);
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
#elif defined(TEENSY4_1)
#else
	FILE *fh = fopen(file_in_user_home(file).c_str(), "rb");
	if (!fh)
		return default_value;
	(void)fread(buffer, 1, 4, fh);
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
#elif defined(TEENSY4_1)
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
#elif defined(TEENSY4_1)
#else
	FILE *fh = fopen(file_in_user_home(file).c_str(), "w");
	if (!fh)
		return false;
	fprintf(fh, "%s", value.c_str());
	fclose(fh);
#endif
	return true;
}

#if IS_POSIX
bool file_exists(const std::string & file)
{
	return access(file.c_str(), F_OK) == 0;
}
#endif

std::string to_hex(const uint8_t *const data, const size_t n_bytes)
{
	std::string out;
	for(size_t i=0; i<n_bytes; i++) {
		if (i)
			out += " ";
		out += format("%02x", data[i]);
	}
	return out;
}
