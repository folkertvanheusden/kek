// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <poll.h>
#include <stdio.h>
#include <ncurses.h>
#include <unistd.h>

#include "console_ncurses.h"
#include "cpu.h"
#include "error.h"
#include "gen.h"
#include "utils.h"


console_ncurses::console_ncurses(std::atomic_uint32_t *const stop_event): console(stop_event)
{
	init_ncurses(true);

	resize_terminal();

	th_panel = new std::thread(&console_ncurses::panel_update_thread, this);
}

console_ncurses::~console_ncurses()
{
	stop_thread();

	if (th_panel) {
		th_panel->join();

		delete th_panel;
	}

	std::unique_lock<std::mutex> lck(ncurses_mutex);

	wattron(w_main->win, A_BOLD);
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

int console_ncurses::wait_for_char_ll(const short timeout)
{
	struct pollfd fds[] = { { STDIN_FILENO, POLLIN, 0 } };

	if (poll(fds, 1, timeout) == 1 && fds[0].revents) {
		std::unique_lock<std::mutex> lck(ncurses_mutex);

		int c = getch();

		if (c == ERR)
			return -1;

		return c;
	}

	return -1;
}

void console_ncurses::put_char_ll(const char c)
{
	if ((c >= 32 && c < 127) || c == 10) {
		std::unique_lock<std::mutex> lck(ncurses_mutex);

		wprintw(w_main->win, "%c", c);

		getyx(w_main->win, ty, tx);

		mydoupdate();
	}
}

void console_ncurses::put_string_lf(const std::string & what)
{
	put_string(what);

	put_string("\n");
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

	create_win_border(0, 27, 100, 5, "panel", &w_panel_b, &w_panel, false);

	scrollok(w_main -> win, TRUE);

	mydoupdate();
}

void console_ncurses::panel_update_thread()
{
	set_thread_name("kek:c-panel");

	cpu *const c = b->getCpu();

	uint64_t prev_instr_cnt = c->get_instructions_executed_count();

	constexpr int refresh_rate = 50;

	while(*stop_event != EVENT_TERMINATE && stop_panel == false) {
		myusleep(1000000 / refresh_rate);

		// note that these are approximately as there's no mutex on the emulation
		try {
			uint16_t current_PSW   = c->getPSW();
			int      run_mode      = current_PSW >> 14;

			uint16_t current_PC    = c->getPC();
			uint32_t full_addr     = b->calculate_physical_address(run_mode, current_PC, false, false, true, i_space);

			uint16_t current_instr = b->readWord(current_PC);

			auto data = c->disassemble(current_PC);

			std::unique_lock<std::mutex> lck(ncurses_mutex);

			werase(w_panel->win);

			//
			wattron(w_panel->win, COLOR_PAIR(1 + run_mode));

			for(uint8_t b=0; b<22; b++)
				mvwprintw(w_panel->win, 0, 1 + 22 - b,      "%c", full_addr     & (1 << b) ? '1' : '0');

			wattron(w_panel->win, COLOR_PAIR(1));

			for(uint8_t b=0; b<16; b++)
				mvwprintw(w_panel->win, 1, 1 + 16 - b,      "%c", current_PSW   & (1 << b) ? '1' : '0');

			for(uint8_t b=0; b<16; b++)
				mvwprintw(w_panel->win, 1, 1 + 16 - b + 17, "%c", current_instr & (1 << b) ? '1' : '0');

			mvwprintw(w_panel->win, 4, 1, "LEDs:");

			uint16_t leds = b->get_console_leds();

			for(uint8_t b=0; b<16; b++)
				mvwprintw(w_panel->win, 4, 1 + 22 - b,      "%c", leds          & (1 << b) ? '1' : '0');

			wattron(w_panel->win, COLOR_PAIR(5));

			mvwprintw(w_panel->win, 1, 1 + 35, "%c%c%c",
				running_flag             ? '+' : '-',
				disk_read_activity_flag  ? '*' : 'o',
				disk_write_activity_flag ? '*' : 'o');

			wattron(w_panel->win, COLOR_PAIR(0));

			// disassembler
			auto registers = data["registers"];
			auto psw       = data["psw"][0];

			std::string instruction_values;
			for(auto & iv : data["instruction-values"])
				instruction_values += (instruction_values.empty() ? "" : ",") + iv;

			std::string work_values;
			for(auto & wv : data["work-values"])
				work_values += (work_values.empty() ? "" : ",") + wv;

			std::string instruction = data["instruction-text"].at(0);

			mvwprintw(w_panel->win, 2, 1, "R0: %s, R1: %s, R2: %s, R3: %s, R4: %s, R5: %s, SP: %s, PC: %s",
					registers[0].c_str(), registers[1].c_str(), registers[2].c_str(), registers[3].c_str(), registers[4].c_str(), registers[5].c_str(),
					registers[6].c_str(), registers[7].c_str()); 
			mvwprintw(w_panel->win, 3, 1, "PSW: %s, instr: %s",
					psw.c_str(),
					instruction_values.c_str());
			mvwprintw(w_panel->win, 3, 46, "%s - %s",
					instruction.c_str(),
					work_values.c_str());
		}
		catch(int trap) {
			std::unique_lock<std::mutex> lck(ncurses_mutex);

			werase(w_panel->win);
		}

		{
			std::unique_lock<std::mutex> lck(ncurses_mutex);

			// speed
			uint64_t cur_instr_cnt = c->get_instructions_executed_count();

			mvwprintw(w_panel->win, 1, 1 + 39, "%8ld", (cur_instr_cnt - prev_instr_cnt) * refresh_rate);

			prev_instr_cnt = cur_instr_cnt;

			// ncurses
			wmove(w_main->win, ty, tx);

			mydoupdate();
		}
	}
}

void console_ncurses::refresh_virtual_terminal()
{
	std::unique_lock<std::mutex> lck(ncurses_mutex);

	wclear(w_main->win);

	for(int row=0; row<t_height; row++)
		mvwprintw(w_main->win, row + 1, 0, "%s", std::string(screen_buffer[row], t_width).c_str());

	mydoupdate();
}
