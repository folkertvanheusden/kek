// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#if defined(_WIN32)
#include "win32.h"
#else
#include <poll.h>
#endif

#include <stdio.h>
#include <unistd.h>

#include "blinkenlights.h"
#include "console_posix.h"
#include "ddp.h"
#include "error.h"
#include "gen.h"


console_posix::console_posix(std::atomic_uint32_t *const stop_event): console(stop_event)
{
#if defined(_WIN32)
	printf("Setting Win32 console to raw mode\n");

        h_in = GetStdHandle(STD_INPUT_HANDLE);
        if (h_in == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Failed to get stdin handle");

        if (!GetConsoleMode(h_in, &original_mode))
            throw std::runtime_error("Failed to get console mode");

        DWORD raw_mode = original_mode;

        // Disable normal cooked input behavior
        raw_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);

        // Disable Quick Edit (prevents console freeze on selection)
        raw_mode &= ~ENABLE_QUICK_EDIT_MODE;
        raw_mode |= ENABLE_EXTENDED_FLAGS;

        // Enable VT input sequences
        raw_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;

        if (!SetConsoleMode(h_in, raw_mode))
            throw std::runtime_error("Failed to set raw mode");

	// enable ANSI processing
	HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD  mode { };
	GetConsoleMode(h_out, &mode);
	mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(h_out, mode);
#else
	if (isatty(STDIN_FILENO)) {
		if (tcgetattr(STDIN_FILENO, &org_tty_opts) == -1)
			error_exit(true, "console_posix: tcgetattr failed");

		struct termios tty_opts_raw { };
		cfmakeraw(&tty_opts_raw);

		if (tcsetattr(STDIN_FILENO, TCSANOW, &tty_opts_raw) == -1)
			error_exit(true, "console_posix: tcsetattr failed");
	}
#endif
}

console_posix::~console_posix()
{
	stop_thread();

	if (th_panel) {
		th_panel->join();
		delete th_panel;
	}

#if defined(_WIN32)
        if (h_in != INVALID_HANDLE_VALUE)
            SetConsoleMode(h_in, original_mode);
#else
	if (isatty(STDIN_FILENO) && tcsetattr(STDIN_FILENO, TCSANOW, &org_tty_opts) == -1)
		error_exit(true, "~console_posix: tcsetattr failed");
#endif
}

void console_posix::begin()
{
	th_panel = new std::thread(&console_posix::panel_update_thread, this);
}

int console_posix::wait_for_char_ll(const int timeout)
{
#if defined(_WIN32)
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(STDIN_FILENO, &rfds);

	timeval to { timeout / 1000, (timeout % 1000) * 1000 };

	if (select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &to) == 1 && FD_ISSET(STDIN_FILENO, &rfds)) {
		INPUT_RECORD record      { };
		DWORD        events_read { };
		ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &record, 1, &events_read);
		if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
			return record.Event.KeyEvent.uChar.AsciiChar;
	}
#else
	struct pollfd fds[] = { { STDIN_FILENO, POLLIN, 0 } };

	if (poll(fds, 1, timeout) == 1 && fds[0].revents) {
		char buffer = 0;
		read(STDIN_FILENO, &buffer, 1);
		return buffer;
	}
#endif

	return -1;
}

void console_posix::put_char_ll(const char c)
{
	putchar(c);
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
	set_thread_name("kek:c-panel");

#if !defined(__APPLE__)
	timespec next { };
	clock_gettime(CLOCK_MONOTONIC, &next);
#endif

	uint64_t add = 1'000'000'000 / refreshrate;
	while(*stop_event != EVENT_TERMINATE && stop_panel == false) {
#if defined(__APPLE__)
		myusleep(add / 1000);
#else
		next.tv_nsec += add;
		while (next.tv_nsec >= 1'000'000'000) {
			next.tv_nsec -= 1'000'000'000;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
#endif

		if (p_blinkenlights) {
			p_blinkenlights->push(b, running_flag);
			if (do_test_panel)
				p_blinkenlights->test();
		}

		if (p_ddp) {
			p_ddp->push(this, b, brightness);
			if (do_test_panel)
				p_ddp->test();
		}

		do_test_panel = false;
	}
}

void console_posix::refresh_virtual_terminal()
{
	printf("%c\n", 12);  // form feed

	for(int row=0; row<t_height; row++)
		printf("%s\n", std::string(screen_buffer[row], t_width).c_str());
}

void console_posix::ui_event_loop()
{
	while(*stop_event != EVENT_HALT && *stop_event != EVENT_INTERRUPT && *stop_event != EVENT_TERMINATE)
               myusleep(1'000'000 / 10);
}
