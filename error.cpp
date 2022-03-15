// (C) 2018 by folkert@vanheusden.com, released under AGPL 3.0
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <regex.h>
#if defined(ESP32)
#include <Arduino.h>
#else
#include <ncursesw/ncurses.h>
#endif

#include "error.h"

[[ noreturn ]] void error_exit(bool sys_err, const char *format, ...)
{
	int e = errno;

#if defined(ESP32)
	Serial.println(format);
#else
	(void)endwin();

	va_list ap;

	va_start(ap, format);
	(void)vfprintf(stderr, format, ap);
	va_end(ap);

	if (sys_err == TRUE)
		fprintf(stderr, "error: %s (%d)\n", strerror(e), e);
#endif

	exit(1);
}
