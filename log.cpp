// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "error.h"
#include "log.h"
#include "utils.h"
#include "win32.h"


static const char *logfile          = strdup("/tmp/kek.log");
log_level_t        log_level_file   = warning;
log_level_t        log_level_screen = warning;
FILE       *lfh              = nullptr;
static int         lf_uid           = -1;
static int         lf_gid           = -1;

#if defined(ESP32)
int gettid()
{
	return 0;
}
#endif

void setlog(const char *lf, const log_level_t ll_file, const log_level_t ll_screen)
{
	if (lfh)
		fclose(lfh);

	free((void *)logfile);

	logfile = lf ? strdup(lf) : nullptr;

	log_level_file = ll_file;
	log_level_screen = ll_screen;

	atexit(closelog);
}

void setloguid(const int uid, const int gid)
{
	lf_uid = uid;
	lf_gid = gid;
}

void closelog()
{
	if (lfh) {
		fclose(lfh);

		lfh = nullptr;
	}
}

void dolog(const log_level_t ll, const char *fmt, ...)
{
#if !defined(BUILD_FOR_RP2040)
	if (!lfh && logfile != nullptr) {
#if !defined(ESP32)
		lfh = fopen(logfile, "a+");
		if (!lfh)
			error_exit(true, "Cannot access log-file %s", logfile);

#if !defined(_WIN32)
		if (lf_uid != -1 && fchown(fileno(lfh), lf_uid, lf_gid) == -1)
			error_exit(true, "Cannot change logfile (%s) ownership", logfile);

		if (fcntl(fileno(lfh), F_SETFD, FD_CLOEXEC) == -1)
			error_exit(true, "fcntl(FD_CLOEXEC) failed");
#endif
#endif
	}

	uint64_t now = get_us();
	time_t t_now = now / 1000000;

	tm tm { 0 };
#if defined(_WIN32)
	tm = *localtime(&t_now);
#else
	if (!localtime_r(&t_now, &tm))
		error_exit(true, "localtime_r failed");
#endif

	char *ts_str = nullptr;

	const char *const ll_names[] = { "debug  ", "info   ", "warning", "error  " };

	asprintf(&ts_str, "%04d-%02d-%02d %02d:%02d:%02d.%06d] %s ",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(now % 1000000),
			ll_names[ll]);

	char *str = nullptr;

	va_list ap;
	va_start(ap, fmt);
	(void)vasprintf(&str, fmt, ap);
	va_end(ap);

#if !defined(ESP32)
	if (ll >= log_level_file && lfh != nullptr)
		fprintf(lfh, "%s%s\n", ts_str, str);
#endif

	if (ll >= log_level_screen)
		printf("%s%s\r\n", ts_str, str);

	free(str);
	free(ts_str);
#endif
}

log_level_t parse_ll(const std::string & str)
{
	if (str == "debug")
		return debug;

	if (str == "info")
		return info;

	if (str == "warning")
		return warning;

	if (str == "error")
		return ll_error;

	if (str == "none")
		return none;

	error_exit(false, "Log level \"%s\" not understood", str.c_str());

	return debug;
}
