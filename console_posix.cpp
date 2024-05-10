// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#if defined(_WIN32)
#include <conio.h>

#include <winsock2.h>
#else
#include <poll.h>
#endif

#include <stdio.h>
#include <unistd.h>

#include "console_posix.h"
#include "error.h"


console_posix::console_posix(std::atomic_uint32_t *const stop_event): console(stop_event)
{
#if !defined(_WIN32)
	if (tcgetattr(STDIN_FILENO, &org_tty_opts) == -1)
		error_exit(true, "console_posix: tcgetattr failed");

	struct termios tty_opts_raw { };
	cfmakeraw(&tty_opts_raw);

	if (tcsetattr(STDIN_FILENO, TCSANOW, &tty_opts_raw) == -1)
		error_exit(true, "console_posix: tcsetattr failed");

	setvbuf(stdin, nullptr, _IONBF, 0);
#endif
}

console_posix::~console_posix()
{
	stop_thread();

#if !defined(_WIN32)
	if (tcsetattr(STDIN_FILENO, TCSANOW, &org_tty_opts) == -1)
		error_exit(true, "~console_posix: tcsetattr failed");
#endif
}

int console_posix::wait_for_char_ll(const short timeout)
{
#if defined(_WIN32)
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(STDIN_FILENO, &rfds);

	timeval to { timeout / 1000000, timeout % 1000000 };

	if (select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &to) == 1 && FD_ISSET(STDIN_FILENO, &rfds))
		return _getch();
#else
	struct pollfd fds[] = { { STDIN_FILENO, POLLIN, 0 } };

	if (poll(fds, 1, timeout) == 1 && fds[0].revents)
		return getchar();
#endif

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
