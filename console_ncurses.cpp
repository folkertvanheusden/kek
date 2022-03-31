#include <poll.h>
#include <stdio.h>
#include <ncurses.h>
#include <unistd.h>

#include "console_ncurses.h"
#include "cpu.h"
#include "error.h"
#include "utils.h"


console_ncurses::console_ncurses(std::atomic_bool *const terminate, bus *const b) :
	console(terminate, b)
{
	init_ncurses(true);

	resize_terminal();

	th       = new std::thread(std::ref(*this));

	th_panel = new std::thread(&console_ncurses::panel_update_thread, this);
}

console_ncurses::~console_ncurses()
{
	if (th_panel) {
		th_panel->join();

		delete th_panel;
	}

	if (th) {
		th->join();

		delete th;
	}

	std::unique_lock<std::mutex> lck(ncurses_mutex);

	wprintw(w_main->win, "\n\n *** PRESS ENTER TO TERMINATE ***\n");
	mydoupdate();

	while(getch() != 13) {
	}

	delete_window(w_panel_b);
	delete_window(w_panel);

	delete_window(w_main_b);
	delete_window(w_main);

	endwin();
}

int console_ncurses::wait_for_char(const int timeout)
{
	struct pollfd fds[] = { { STDIN_FILENO, POLLIN, 0 } };

	if (poll(fds, 1, timeout) == 1 && fds[0].revents) {
		std::unique_lock<std::mutex> lck(ncurses_mutex);

		return getch();
	}

	return -1;
}

void console_ncurses::put_char_ll(const char c)
{
	if (c >= 32 || (c != 12 && c != 27 && c != 13)) {
		std::unique_lock<std::mutex> lck(ncurses_mutex);

		wprintw(w_main->win, "%c", c);

		getyx(w_main->win, ty, tx);

		mydoupdate();
	}
}

void console_ncurses::resize_terminal()
{
	std::unique_lock<std::mutex> lck(ncurses_mutex);

	determine_terminal_size();

	if (ERR == resizeterm(max_y, max_x))
		error_exit(true, "problem resizing terminal");

	wresize(stdscr, max_y, max_x);

	endwin();
	refresh();

	init_pair(1, COLOR_RED,    COLOR_BLACK);
	init_pair(2, COLOR_YELLOW, COLOR_BLACK);
	init_pair(3, COLOR_BLUE,   COLOR_BLACK);
	init_pair(4, COLOR_GREEN,  COLOR_BLACK);
	init_pair(5, COLOR_CYAN,   COLOR_BLACK);

	wclear(stdscr);

	delete_window(w_main_b);
	delete_window(w_main);

	create_win_border(0, 0, 80, 25, "terminal", &w_main_b, &w_main, false);

	create_win_border(0, 26, 80, 3, "panel", &w_panel_b, &w_panel, false);

	scrollok(w_main -> win, TRUE);

	mydoupdate();
}

void console_ncurses::panel_update_thread()
{
	cpu *const c = b->getCpu();

	while(!*terminate) {
		myusleep(1000000 / 50);  // 50 updates/sec

		// note that these are approximately as there's no mutex on the emulation
		uint16_t current_PC    = c->getPC();
		uint32_t full_addr     = b->calculate_full_address(current_PC);

		uint16_t current_instr = b->readWord(current_PC);

		uint16_t current_PSW   = c->getPSW();

		std::unique_lock<std::mutex> lck(ncurses_mutex);

		wattron(w_panel->win, COLOR_PAIR(1 + (current_PSW >> 14)));

		for(uint8_t b=0; b<22; b++)
			mvwprintw(w_panel->win, 0, 1 + 22 - b,      "%c", full_addr     & (1 << b) ? '1' : '0');

		wattron(w_panel->win, COLOR_PAIR(1));

		for(uint8_t b=0; b<16; b++)
			mvwprintw(w_panel->win, 1, 1 + 16 - b,      "%c", current_PSW   & (1 << b) ? '1' : '0');

		for(uint8_t b=0; b<16; b++)
			mvwprintw(w_panel->win, 1, 1 + 16 - b + 17, "%c", current_instr & (1 << b) ? '1' : '0');

		wattron(w_panel->win, COLOR_PAIR(5));

		mvwprintw(w_panel->win, 1, 1 + 35, "%c%c%c",
			running_flag             ? '+' : '-',
			disk_read_activity_flag  ? '*' : 'o',
			disk_write_activity_flag ? '*' : 'o');

		wattron(w_panel->win, COLOR_PAIR(0));

		wmove(w_main->win, ty, tx);

		mydoupdate();
	}
}
