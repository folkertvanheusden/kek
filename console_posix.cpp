#include <poll.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "console_posix.h"
#include "error.h"


console_posix::console_posix(std::atomic_bool *const terminate) : console(terminate)
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
	if (tcsetattr(STDIN_FILENO, TCSANOW, &org_tty_opts) == -1)
		error_exit(true, "~console_posix: tcsetattr failed");
}

int console_posix::wait_for_char(const int timeout)
{
	struct pollfd fds[] = { { STDIN_FILENO, POLLIN, timeout } };

	if (poll(fds, 1, 0) == 1 && fds[0].revents)
		return getchar();

	return -1;
}

void console_posix::put_char_ll(const char c)
{
	printf("%c", c);

	fflush(nullptr);
}

void console_posix::resize_terminal()
{
}
