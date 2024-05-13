// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <string>

#include "gen.h"
#if defined(ESP32)
#include "FvHNTP/FvHNTP.h"
#endif


class console;

typedef enum { ll_emerg = 0, ll_alert, ll_critical, ll_error, warning, notice, info, debug, none } log_level_t;  // TODO ll_ prefix

log_level_t parse_ll(const std::string & str);
void setlogfile(const char *const lf, const log_level_t ll_file, const log_level_t ll_screen, const bool l_timestamp);
bool setloghost(const char *const host, const log_level_t ll);
void setll(const log_level_t ll_screen, const log_level_t ll_file);
void setloguid(const int uid, const int gid);
void send_syslog(const int ll, const std::string & what);
void closelog();
void dolog(const log_level_t ll, const char *fmt, ...);
void settrace(const bool on);
bool gettrace();
#if defined(ESP32)
void set_clock_reference(ntp *const ntp_);
#endif
void set_terminal(console *const cnsl);

#ifdef TURBO
#define DOLOG(ll, always, fmt, ...) do { } while(0)
#else
#if defined(ESP32)
#define DOLOG(ll, always, fmt, ...) do {				\
	extern log_level_t log_level_file, log_level_screen;		\
									\
	if (always || ll <= log_level_file || ll <= log_level_screen)   \
		dolog(ll, fmt, ##__VA_ARGS__);				\
	} while(0)
#else
#define DOLOG(ll, always, fmt, ...) do {				\
	extern log_level_t log_level_file, log_level_screen;		\
									\
	if (always || ll <= log_level_file || ll <= log_level_screen) [[unlikely]] \
		dolog(ll, fmt, ##__VA_ARGS__);				\
	} while(0)
#endif
#endif

#ifdef TURBO
#define TRACE(fmt, ...) do { } while(0)
#else
#define TRACE(fmt, ...) do {			\
	extern bool log_trace_enabled;		\
	if (log_trace_enabled) {		\
		dolog(debug, fmt, ##__VA_ARGS__);	\
	}					\
} while(0)
#endif
