// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "error.h"
#include "log.h"
#include "utils.h"
#if defined(_WIN32)
#include "win32.h"
#endif


static const char *logfile          = strdup("/tmp/kek.log");
static sockaddr_in syslog_ip_addr   = { };
static bool        is_file          = true;
log_level_t        log_level_file   = warning;
log_level_t        log_level_screen = warning;
static FILE       *log_fh           = nullptr;
static int         lf_uid           = -1;
static int         lf_gid           = -1;
static bool        l_timestamp      = true;
static int         log_buffer_size  = 128;
static char       *log_buffer       = reinterpret_cast<char *>(malloc(log_buffer_size));

#if defined(ESP32)
int gettid()
{
	return 0;
}
#endif

void setlogfile(const char *const lf, const log_level_t ll_file, const log_level_t ll_screen, const bool timestamp)
{
	if (log_fh)
		fclose(log_fh);

	free((void *)logfile);

	is_file = true;

	logfile = lf ? strdup(lf) : nullptr;

	log_level_file   = ll_file;
	log_level_screen = ll_screen;

	l_timestamp      = timestamp;

	atexit(closelog);
}

bool setloghost(const char *const host, const log_level_t ll)
{
	syslog_ip_addr.sin_family = AF_INET;
	bool ok = inet_aton(host, &syslog_ip_addr.sin_addr) == 1;
	syslog_ip_addr.sin_port   = htons(514);

	is_file        = false;

	log_level_file = ll;

	l_timestamp    = false;

	return ok;
}

void setll(const log_level_t ll_screen, const log_level_t ll_file)
{
	log_level_file   = ll_file;
	log_level_screen = ll_screen;
}

void setloguid(const int uid, const int gid)
{
	lf_uid = uid;
	lf_gid = gid;
}

void send_syslog(const int ll, const std::string & what)
{
	std::string msg = format("<%d>%s", 16 * 8 + ll, what.c_str());

	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s != -1) {
		(void)sendto(s, msg.c_str(), msg.size(), 0, reinterpret_cast<sockaddr *>(&syslog_ip_addr), sizeof syslog_ip_addr);
		close(s);
	}
}

void closelog()
{
	if (log_fh) {
		fclose(log_fh);

		log_fh = nullptr;
	}
}

void dolog(const log_level_t ll, const char *fmt, ...)
{
#if !defined(BUILD_FOR_RP2040)
	if (!log_fh && logfile != nullptr) {
#if !defined(ESP32)
		log_fh = fopen(logfile, "a+");
		if (!log_fh)
			error_exit(true, "Cannot access log-file %s", logfile);
#if !defined(_WIN32)
		if (lf_uid != -1 && fchown(fileno(log_fh), lf_uid, lf_gid) == -1)
			error_exit(true, "Cannot change logfile (%s) ownership", logfile);

		if (fcntl(fileno(log_fh), F_SETFD, FD_CLOEXEC) == -1)
			error_exit(true, "fcntl(FD_CLOEXEC) failed");
#endif
#endif
	}

	for(;;) {
		va_list ap;
		va_start(ap, fmt);
		int needed_length = vsnprintf(log_buffer, log_buffer_size, fmt, ap);
		va_end(ap);

		if (needed_length < log_buffer_size)
			break;

		log_buffer_size *= 2;
		log_buffer = reinterpret_cast<char *>(realloc(log_buffer, log_buffer_size));
	}

	if (l_timestamp) {
		uint64_t now = get_us();
		time_t t_now = now / 1000000;

		tm tm { 0 };
#if defined(_WIN32)
		tm = *localtime(&t_now);
#else
		if (!localtime_r(&t_now, &tm))
			error_exit(true, "localtime_r failed");
#endif
		char ts_str[64] { };

		const char *const ll_names[] = { "emerg  ", "alert  ", "crit   ", "error  ", "warning", "notice ", "info   ", "debug  ", "none   " };

		snprintf(ts_str, sizeof ts_str, "%04d-%02d-%02d %02d:%02d:%02d.%06d %s|%s] ",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(now % 1000000),
				ll_names[ll], get_thread_name().c_str());

		if (ll <= log_level_file && is_file == false)
			send_syslog(ll, log_buffer);
#if !defined(ESP32)
		if (ll <= log_level_file && log_fh != nullptr)
			fprintf(log_fh, "%s%s\n", ts_str, log_buffer);
#endif

		if (ll <= log_level_screen)
			printf("%s%s\r\n", ts_str, log_buffer);
	}
	else {
		if (ll <= log_level_file && is_file == false)
			send_syslog(ll, log_buffer);
#if !defined(ESP32)
		if (ll <= log_level_file && log_fh != nullptr)
			fprintf(log_fh, "%s\n", log_buffer);
#endif

		if (ll <= log_level_screen)
			printf("%s\r\n", log_buffer);
	}
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
