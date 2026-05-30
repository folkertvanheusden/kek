// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
#include <Arduino.h>
#elif defined(_WIN32)
#elif defined(__FreeBSD__)
#include <ncurses/ncurses.h>
#else
#include <ncursesw/ncurses.h>
#endif

#include "error.h"

[[ noreturn ]] void error_exit(bool sys_err, const char *format, ...)
{
#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
	printf("Fatal error: %s\n", format);
#else
	int e = errno;

#if !defined(_WIN32)
	(void)endwin();
#endif

	va_list ap;

	va_start(ap, format);
	(void)vfprintf(stderr, format, ap);
	va_end(ap);

	if (sys_err == true)
		fprintf(stderr, "error: %s (%d)\n", strerror(e), e);

	exit(1);
#endif
}
