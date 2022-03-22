#include <poll.h>
#include <stdio.h>
#include <ncurses.h>
#include <unistd.h>

#include "console_ncurses.h"
#include "error.h"


console_ncurses::console_ncurses(std::atomic_bool *const terminate) : console(terminate)
{
	init_ncurses(true);

	resize_terminal();

	th = new std::thread(std::ref(*this));
}

console_ncurses::~console_ncurses()
{
	if (th) {
		th->join();

		delete th;
	}

	wprintw(w_main->win, "\n\n *** PRESS ENTER TO TERMINATE ***\n");
	mydoupdate();

	while(getch() != 13) {
	}

	endwin();
}

int console_ncurses::wait_for_char(const int timeout)
{
	struct pollfd fds[] = { { STDIN_FILENO, POLLIN, timeout } };

	if (poll(fds, 1, 0) == 1 && fds[0].revents)
		return getch();

	return -1;
}

void console_ncurses::put_char_ll(const char c)
{
	if (c >= 32 || (c != 12 && c != 27 && c != 13)) {
		wprintw(w_main->win, "%c", c);

		mydoupdate();
	}
}

void console_ncurses::resize_terminal()
{
	determine_terminal_size();

	if (ERR == resizeterm(max_y, max_x))
		error_exit(true, "problem resizing terminal");

	wresize(stdscr, max_y, max_x);

	endwin();
	refresh();

	wclear(stdscr);

	delete_window(w_main_b);
	delete_window(w_main);

	create_win_border(0, 0, max_x - 2, max_y - 2, "window", &w_main_b, &w_main, false);

	scrollok(w_main -> win, TRUE);

	mydoupdate();
}
