// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <string>

#include "gen.h"


class console;

// ss = subsystem
enum class log_ss : uint64_t {
	LS_GENERIC = 1,
	LS_CPU     = 2,
	LS_DEQNA   = 4,
	LS_ETH     = 8,
	LS_TRACE   = 16,
	LS_BLINKEN = 32,
	LS_BUS     = 64,
	LS_COMM    = 128,
	LS_DISK    = 256,
	LS_MMU     = 512,
	LS_TAPE    = 1024,
};

void setlogfile(const char *const lf, const bool l_timestamp);
bool setloghost(const char *const host);
void setloguid(const int uid, const int gid);
void send_syslog(const std::string & what);
void closelog();
void dolog(const log_ss ls, const char *fmt, ...);
#if defined(ESP32)
void set_clock_reference(const char *const ntp_server);
#endif
void set_terminal(console *const cnsl);
bool is_terminal_set();
bool toggle_ss_log(const std::string & name);
void set_ss_log(const log_ss ls);
std::string get_log_mask();
void disable_all_lss();
std::string get_all_masks();
uint64_t get_masks();

#ifdef TURBO
#define DOLOG(ls, fmt, ...) do { } while(0)
#else
#define DOLOG(ls, fmt, ...) do {		\
	extern uint64_t log_mask;		\
	if (log_mask & uint64_t(ls))            \
		dolog(ls, fmt, ##__VA_ARGS__);	\
	} while(0) 
#endif
