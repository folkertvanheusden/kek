#pragma once

#include <string>


typedef enum { debug, info, warning, ll_error, none } log_level_t;  // TODO ll_ prefix

log_level_t parse_ll(const std::string & str);
void setlog(const char *lf, const log_level_t ll_file, const log_level_t ll_screen);
void setloguid(const int uid, const int gid);
void closelog();
void dolog(const log_level_t ll, const char *fmt, ...);

#define DOLOG(ll, always, fmt, ...) do {				\
	extern log_level_t log_level_file, log_level_screen;		\
									\
	if (always && (ll >= log_level_file || ll >= log_level_screen))	\
		dolog(ll, fmt, ##__VA_ARGS__);				\
	} while(0)
