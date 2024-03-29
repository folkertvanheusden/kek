// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <string>

#include "config.h"


typedef enum { debug, info, warning, ll_error, none } log_level_t;  // TODO ll_ prefix

log_level_t parse_ll(const std::string & str);
void setlog(const char *lf, const log_level_t ll_file, const log_level_t ll_screen, const bool l_timestamp);
void setloguid(const int uid, const int gid);
void closelog();
void dolog(const log_level_t ll, const char *fmt, ...);

#ifdef TURBO
#define DOLOG(ll, always, fmt, ...) do { } while(0)
#else
#define DOLOG(ll, always, fmt, ...) do {				\
	extern log_level_t log_level_file, log_level_screen;		\
									\
	if (always || ll >= log_level_file || ll >= log_level_screen)	\
		dolog(ll, fmt, ##__VA_ARGS__);				\
	} while(0)
#endif
