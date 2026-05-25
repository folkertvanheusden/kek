// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <string>

#include "gen.h"


class console;

// ss = subsystem
typedef uint32_t log_ss_type;
enum class log_ss : log_ss_type {
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
bool toggle_ss_log(const bool is_console, const std::string & name);
void set_ss_log(const bool is_console, const log_ss ls);
std::string get_ss_mask(const bool is_console);
void disable_all_log_ss(const bool is_console);
log_ss_type get_log_ss_masks(const bool is_console);
std::string get_all_available_log_ss_masks();

#ifdef TURBO
#define DOLOG(ls, fmt, ...) do { } while(0)
#else
#define DOLOG(ls, fmt, ...) do {		      \
	extern log_ss_type log_mask_match;	      \
	if (log_mask_match & log_ss_type(ls))         \
		dolog(ls, fmt, ##__VA_ARGS__);	      \
	} while(0) 
#endif
