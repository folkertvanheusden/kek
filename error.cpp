// (C) 2018 by folkert@vanheusden.com, released under AGPL 3.0
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <regex.h>
#include <ncursesw/ncurses.h>

#include "error.h"

[[ noreturn ]] void error_exit(bool sys_err, const char *format, ...)
{
	int e = errno;
	va_list ap;

	(void)endwin();

	va_start(ap, format);
	(void)vfprintf(stderr, format, ap);
	va_end(ap);

	if (sys_err == TRUE)
		fprintf(stderr, "error: %s (%d)\n", strerror(e), e);

	exit(1);
}
