#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "error.h"
#include "log.h"
#include "utils.h"


static const char *logfile          = strdup("/tmp/myip.log");
log_level_t        log_level_file   = warning;
log_level_t        log_level_screen = warning;
static FILE       *lfh              = nullptr;
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
}

void setloguid(const int uid, const int gid)
{
	lf_uid = uid;
	lf_gid = gid;
}

void closelog()
{
	fclose(lfh);

	lfh = nullptr;
}

void dolog(const log_level_t ll, const char *fmt, ...)
{
	if ((ll < log_level_file && ll < log_level_screen) || !logfile)
		return;

	if (!lfh) {
		lfh = fopen(logfile, "a+");
		if (!lfh)
			error_exit(true, "Cannot access log-file %s", logfile);

#if !defined(ESP32)
		if (lf_uid != -1 && fchown(fileno(lfh), lf_uid, lf_gid) == -1)
			error_exit(true, "Cannot change logfile (%s) ownership", logfile);

		if (fcntl(fileno(lfh), F_SETFD, FD_CLOEXEC) == -1)
			error_exit(true, "fcntl(FD_CLOEXEC) failed");
#endif
	}

	uint64_t now = get_us();
	time_t t_now = now / 1000000;

	struct tm tm { 0 };
	if (!localtime_r(&t_now, &tm))
		error_exit(true, "localtime_r failed");

	char *ts_str = nullptr;

	const char *const ll_names[] = { "debug  ", "info   ", "warning", "error  " };

	asprintf(&ts_str, "%04d-%02d-%02d %02d:%02d:%02d.%06d %.6f|%d] %s ",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(now % 1000000),
			get_us() / 1000000.0, gettid(), ll_names[ll]);

	char *str = nullptr;

	va_list ap;
	va_start(ap, fmt);
	(void)vasprintf(&str, fmt, ap);
	va_end(ap);

	if (ll >= log_level_file)
		fprintf(lfh, "%s%s\n", ts_str, str);

	if (ll >= log_level_screen)
		printf("%s%s\r\n", ts_str, str);

	free(str);
	free(ts_str);
}

