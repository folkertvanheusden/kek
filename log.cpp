// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(_WIN32)
#include <ws2tcpip.h>
#include <winsock2.h>
#else
#if defined(ESP32)
#include <Arduino.h>
#include "esp_sntp.h"
#endif
#if defined(BUILD_FOR_PICO2W)
#include <Arduino.h>
#include <WiFiUdp.h>
#elif defined(TEENSY4_1)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#endif
#include <sys/types.h>

#include "console.h"
#include "error.h"
#include "log.h"
#include "utils.h"
#if defined(_WIN32)
#include "win32.h"
#endif


static const char  *logfile           = strdup("/tmp/kek.log");
#if defined(BUILD_FOR_PICO2W)
static WiFiUDP      udp;
static std::string  syslog_host;
#elif defined(TEENSY4_1)
static qn::EthernetUDP udp;
static std::string  syslog_host;
#else
static sockaddr_in  syslog_ip_addr    = { };
#endif
static bool         is_syslog         = false;
static FILE        *log_fh            = nullptr;
static int          lf_uid            = -1;
static int          lf_gid            = -1;
static bool         l_timestamp       = true;
char                dummy_buffer[2] { 0 };
static console     *log_cnsl          = nullptr;
log_ss_type         log_mask_c        = 0;
log_ss_type         log_mask_f        = 0;
log_ss_type         log_mask_match    = log_mask_c | log_mask_f;
constexpr const char *const ls_names[] { "GENERIC", "CPU", "DEQNA", "ETH", "TRACE", "BLINKEN", "BUS", "COMM", "DISK", "MMU", "TAPE" };


void disable_all_log_ss(const bool console)
{
	if (console)
		log_mask_c = 0;
	else
		log_mask_f = 0;
	log_mask_match = log_mask_c | log_mask_f;
}

void set_ss_log(const bool console, const log_ss ls)
{
	(console ? log_mask_c : log_mask_f) |= log_ss_type(ls);
	log_mask_match = log_mask_c | log_mask_f;
}

log_ss_type get_log_ss_masks(const bool console)
{
	return console ? log_mask_c : log_mask_f;
}

bool toggle_ss_log(const bool console, const std::string & name)
{
	log_ss_type mask = 0;

	if (name == "generic")
		mask = log_ss_type(log_ss::LS_GENERIC);
	else if (name == "cpu")
		mask = log_ss_type(log_ss::LS_CPU);
	else if (name == "deqna")
		mask = log_ss_type(log_ss::LS_DEQNA);
	else if (name == "eth")
		mask = log_ss_type(log_ss::LS_ETH);
	else if (name == "trace")
		mask = log_ss_type(log_ss::LS_TRACE);
	else if (name == "blinken" || name == "blinkenlights" || name == "bl")
		mask = log_ss_type(log_ss::LS_BLINKEN);
	else if (name == "bus")
		mask = log_ss_type(log_ss::LS_BUS);
	else if (name == "comm")
		mask = log_ss_type(log_ss::LS_COMM);
	else if (name == "disk")
		mask = log_ss_type(log_ss::LS_DISK);
	else if (name == "mmu")
		mask = log_ss_type(log_ss::LS_MMU);
	else if (name == "tape")
		mask = log_ss_type(log_ss::LS_TAPE);
	else
		return false;

	auto & log_mask = console ? log_mask_c : log_mask_f;
	if (log_mask & mask)
		log_mask &= ~mask;
	else
		log_mask |= mask;
	log_mask_match = log_mask_c | log_mask_f;

	return true;
}

std::string get_ss_mask(const bool console)
{
	auto log_mask = console ? log_mask_c : log_mask_f;

	std::string out;
	if (log_mask & log_ss_type(log_ss::LS_GENERIC))
		out += "generic,";
	if (log_mask & log_ss_type(log_ss::LS_CPU))
		out += "cpu,";
	if (log_mask & log_ss_type(log_ss::LS_DEQNA))
		out += "deqna,";
	if (log_mask & log_ss_type(log_ss::LS_ETH))
		out += "eth,";
	if (log_mask & log_ss_type(log_ss::LS_TRACE))
		out += "trace,";
	if (log_mask & log_ss_type(log_ss::LS_BLINKEN))
		out += "blinken,";
	if (log_mask & log_ss_type(log_ss::LS_BUS))
		out += "bus,";
	if (log_mask & log_ss_type(log_ss::LS_COMM))
		out += "comm,";
	if (log_mask & log_ss_type(log_ss::LS_DISK))
		out += "disk,";
	if (log_mask & log_ss_type(log_ss::LS_MMU))
		out += "mmu,";
	if (log_mask & log_ss_type(log_ss::LS_TAPE))
		out += "tape,";
	return out;
}

std::string get_all_available_log_ss_masks()
{
	return "generic,cpu,deqna,eth,trace,blinken,bus,comm,disk,mmu,tape";
}

#if defined(ESP32)
int gettid()
{
	return 0;
}
#endif

#if defined(ESP32)
void set_clock_reference(const char *const ntp_server)
{
	configTime(0, 0, ntp_server, "gateway.vanheusden.com");
}
#endif

void set_terminal(console *const cnsl)
{
	log_cnsl = cnsl;
}

bool is_terminal_set()
{
	return log_cnsl;
}

void setlogfile(const char *const lf, const bool timestamp)
{
	if (log_fh)
		fclose(log_fh);

	free((void *)logfile);

	logfile     = lf ? strdup(lf) : nullptr;
	l_timestamp = timestamp;

	atexit(closelog);
}

bool setloghost(const char *const host)
{
	is_syslog   = true;
	l_timestamp = false;

#if defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
	syslog_host    = host;
	return true;
#else
	syslog_ip_addr.sin_family = AF_INET;
	bool ok = inet_pton(AF_INET, host, &syslog_ip_addr.sin_addr) == 1;
	syslog_ip_addr.sin_port   = htons(514);
	return ok;
#endif
}

void setloguid(const int uid, const int gid)
{
	lf_uid = uid;
	lf_gid = gid;
}

void send_syslog(const std::string & what)
{
	std::string msg = format("<%d>PDP11 %s", 16 * 8 + 6 /* info */, what.c_str());

#if defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
        udp.beginPacket(syslog_host.c_str(), 514);
        udp.write(msg.c_str(), msg.size());
        udp.endPacket();
#else
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s != -1) {
		(void)sendto(s, msg.c_str(), msg.size(), 0, reinterpret_cast<sockaddr *>(&syslog_ip_addr), sizeof syslog_ip_addr);
		close(s);
	}
#endif
}

void closelog()
{
	if (log_fh) {
		fclose(log_fh);
		log_fh = nullptr;
	}
}

void dolog(const log_ss ls, const char *fmt, ...)
{
#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1) && !defined(ESP32)
	if (!log_fh && logfile != nullptr) {
		log_fh = fopen(logfile, "a+");
		if (!log_fh)
			error_exit(true, "Cannot access log-file %s", logfile);
#if !defined(_WIN32)
		if (lf_uid != -1 && fchown(fileno(log_fh), lf_uid, lf_gid) == -1)
			error_exit(true, "Cannot change logfile (%s) ownership", logfile);

		if (fcntl(fileno(log_fh), F_SETFD, FD_CLOEXEC) == -1)
			error_exit(true, "fcntl(FD_CLOEXEC) failed");
#endif
	}
#endif

	bool log_file    = log_mask_f & log_ss_type(ls);
	bool log_console = log_mask_c & log_ss_type(ls);

	va_list ap;
	va_start(ap, fmt);
	ssize_t needed_length = vsnprintf(dummy_buffer, 1, fmt, ap);
	va_end(ap);
	char *const log_buffer = new char[needed_length + 1];
	va_start(ap, fmt);
	vsnprintf(log_buffer, needed_length + 1, fmt, ap);
	va_end(ap);

	if (l_timestamp) {
		uint64_t now   = get_us();
		time_t   t_now = now / 1000000;

		tm tm { };
#if defined(_WIN32)
		_localtime64_s(&tm, &t_now);
#else
		if (!localtime_r(&t_now, &tm))
			error_exit(true, "localtime_r failed");
#endif
#if defined(BUILD_FOR_PICO2W)
		const char *ls_name = "?";
		const auto  ls_int  = log_ss_type(ls);
		for(int i=0; i<32; i++) {
			if (ls_int & (1 << i)) {
				ls_name = ls_names[i];
				break;
			}
		}
#else
		const char *ls_name = ls_names[std::countr_zero(log_ss_type(ls))];
#endif
		char        ts_str[64] { };
		snprintf(ts_str, sizeof ts_str, "%04d-%02d-%02d %02d:%02d:%02d.%06d %-7s|%s] ",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(now % 1000000),
				ls_name, get_thread_name().c_str());

		if (is_syslog)
			send_syslog(log_buffer);
#if !defined(ESP32)
		if (log_fh != nullptr && log_file)
			fprintf(log_fh, "%s%s\n", ts_str, log_buffer);
#endif

		if (log_console) {
			if (log_cnsl) {
				log_cnsl->put_string(ts_str);
				log_cnsl->put_string_lf(log_buffer);
			}
			else {
				printf("%s%s\r\n", ts_str, log_buffer);
			}
		}
	}
	else {
		if (is_syslog)
			send_syslog(log_buffer);
#if !defined(ESP32)
		if (log_fh != nullptr && log_file)
			fprintf(log_fh, "%s\n", log_buffer);
#endif

		if (log_console) {
			if (log_cnsl)
				log_cnsl->put_string_lf(log_buffer);
			else
				printf("%s\r\n", log_buffer);
		}
	}

	delete [] log_buffer;
}
