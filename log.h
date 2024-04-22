// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <string>

#include "config.h"


typedef enum { ll_emerg = 0, ll_alert, ll_critical, ll_error, warning, notice, info, debug, none } log_level_t;  // TODO ll_ prefix

log_level_t parse_ll(const std::string & str);
void setlogfile(const char *const lf, const log_level_t ll_file, const log_level_t ll_screen, const bool l_timestamp);
void setloghost(const char *const host, const log_level_t ll);
void setll(const log_level_t ll_file, const log_level_t ll_screen);
void setloguid(const int uid, const int gid);
void closelog();
void dolog(const log_level_t ll, const char *fmt, ...);

#ifdef TURBO
#define DOLOG(ll, always, fmt, ...) do { } while(0)
#else
#define DOLOG(ll, always, fmt, ...) do {				\
	extern log_level_t log_level_file, log_level_screen;		\
									\
	[[unlikely]]							\
	if (always || ll <= log_level_file || ll <= log_level_screen) 	\
		dolog(ll, fmt, ##__VA_ARGS__);				\
	} while(0)
#endif
