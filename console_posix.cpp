// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <poll.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "console_posix.h"
#include "error.h"


console_posix::console_posix(std::atomic_uint32_t *const stop_event, bus *const b) :
	console(stop_event, b)
{
	if (tcgetattr(STDIN_FILENO, &org_tty_opts) == -1)
		error_exit(true, "console_posix: tcgetattr failed");

	struct termios tty_opts_raw { 0 };
	cfmakeraw(&tty_opts_raw);

	if (tcsetattr(STDIN_FILENO, TCSANOW, &tty_opts_raw) == -1)
		error_exit(true, "console_posix: tcsetattr failed");
}

console_posix::~console_posix()
{
	stop_thread();

	if (tcsetattr(STDIN_FILENO, TCSANOW, &org_tty_opts) == -1)
		error_exit(true, "~console_posix: tcsetattr failed");
}

int console_posix::wait_for_char_ll(const short timeout)
{
	struct pollfd fds[] = { { STDIN_FILENO, POLLIN, timeout } };

	if (poll(fds, 1, timeout) == 1 && fds[0].revents)
		return getchar();

	return -1;
}

void console_posix::put_char_ll(const char c)
{
	printf("%c", c);

	fflush(nullptr);
}

void console_posix::put_string_lf(const std::string & what)
{
	put_string(what + "\r\n");
}

void console_posix::resize_terminal()
{
}

void console_posix::panel_update_thread()
{
}

void console_posix::refresh_virtual_terminal()
{
	printf("%c\n", 12);  // form feed

	for(int row=0; row<t_height; row++)
		printf("%s\n", std::string(screen_buffer[row], t_width).c_str());
}
