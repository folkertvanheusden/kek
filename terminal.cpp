// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <algorithm>
#include <assert.h>
#include <ncursesw/curses.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/ioctl.h>

#include "terminal.h"
#include "error.h"
#include "utils.h"

int max_x = 80, max_y = 25;

void determine_terminal_size()
{
	struct winsize size;

	if (ioctl(1, TIOCGWINSZ, &size) == 0)
	{
		max_y = size.ws_row;
		max_x = size.ws_col;
	}
	else
	{
		char *dummy = getenv("COLUMNS");
		if (dummy)
			max_x = atoi(dummy);

		dummy = getenv("LINES");
		if (dummy)
			max_x = atoi(dummy);
	}
}

void apply_mouse_setting(void)
{
	mousemask(0, nullptr);
}

void init_ncurses(bool init_mouse)
{
        initscr();
	start_color();
	use_default_colors();
        keypad(stdscr, TRUE);
        cbreak();
        intrflush(stdscr, FALSE);
        noecho();
        nonl();
        refresh();
        nodelay(stdscr, TRUE);
        meta(stdscr, TRUE);     /* enable 8-bit input */
        raw();  /* to be able to catch ctrl+c */
	leaveok(stdscr, FALSE);

	if (init_mouse)
		apply_mouse_setting();

        max_y = LINES;
        max_x = COLS;
}

void wrong_key(void)
{
	flash();
	beep();
	flushinp();
}

void color_on(NEWWIN *win, int pair)
{
	wattron(win -> win, COLOR_PAIR(pair));
}

void color_off(NEWWIN *win, int pair)
{
	wattroff(win -> win, COLOR_PAIR(pair));
}

void delete_window(NEWWIN *mywin)
{
	mydelwin(mywin);

	free(mywin);
}

void mydelwin(NEWWIN *win)
{
	if (win)
	{
		if (win -> pwin && ERR == del_panel(win -> pwin))
			error_exit(false, "del_panel() failed");

		if (win -> win && ERR == delwin(win -> win))
			error_exit(false, "delwin() failed");
	}
}

void mydoupdate()
{
	update_panels();

	doupdate();
}

WINDOW * mynewwin(int nlines, int ncols, int y, int x)
{
	assert(x >= 0 && y >= 0);
	assert(x < max_x && y < max_y);
	assert(x + ncols <= max_x && y + nlines <= max_y);

        WINDOW *dummy = newwin(nlines, ncols, y, x);
        if (!dummy)
                error_exit(false, "failed to create window (subwin) with dimensions %d-%d at offset %d,%d (terminal size: %d,%d)", ncols, nlines, x, y, max_x, max_y);

	keypad(dummy, TRUE);
	leaveok(dummy, TRUE);

        return dummy;
}

NEWWIN * create_window(int n_lines, int n_colls)
{
        return create_window_xy((max_y/2) - (n_lines/2), (max_x/2) - (n_colls/2), n_lines, n_colls);
}

NEWWIN * create_window_xy(int y, int x, int n_lines, int n_colls)
{
	assert(x >= 0 && y >= 0);
	assert(x < max_x && y < max_y);
	assert(x + n_colls <= max_x && y + n_lines <= max_y);
        NEWWIN *newwin = (NEWWIN *)malloc(sizeof(NEWWIN));

        /* create new window */
        newwin -> win = mynewwin(n_lines, n_colls, y, x);
	newwin -> pwin = new_panel(newwin -> win);
        werase(newwin -> win);

	newwin -> ncols = n_colls;
	newwin -> nlines = n_lines;

	newwin -> x = x;
	newwin -> y = y;

        return newwin;
}

void limit_print(NEWWIN *win, int width, int y, int x, char *format, ...)
{
        va_list ap;
	int len = 0;
	char *buf = nullptr;

        va_start(ap, format);
	len = vasprintf(&buf, format, ap);
        va_end(ap);

	if (len > width)
		buf[width] = 0x00;

	mvwprintw(win -> win, y, x, "%s", buf);

	free(buf);
}

void escape_print_xy(NEWWIN *win, int y, int x, char *str)
{
	int loop = 0, cursor_x = 0, len = strlen(str);
	bool inv = false, underline = false;

	for(loop=0; loop<len; loop++)
	{
		if (str[loop] == '^')
		{
			if (!inv)
				mywattron(win -> win, A_REVERSE);
			else
				mywattroff(win -> win, A_REVERSE);

			inv = 1 - inv;
		}
		else if (str[loop] == '_')
		{
			if (!underline)
				mywattron(win -> win, A_UNDERLINE);
			else
				mywattroff(win -> win, A_UNDERLINE);

			underline = 1 - underline;
		}
		else if (str[loop] == '\n')
		{
			cursor_x = 0;
			y++;
		}
		else
		{
			mvwprintw(win -> win, y, x + cursor_x++, "%c", str[loop]);
		}
	}

	if (inv)
		mywattroff(win -> win, A_REVERSE);

	if (underline)
		mywattroff(win -> win, A_UNDERLINE);
}

void escape_print(NEWWIN *win, const char *str, const char rev, const char un)
{
	int loop, len = strlen(str);
	bool inv = false, underline = false;

	for(loop=0; loop<len; loop++)
	{
		if (str[loop] == rev)
		{
			if (!inv)
				mywattron(win -> win, A_REVERSE);
			else
				mywattroff(win -> win, A_REVERSE);

			inv = 1 - inv;
		}
		else if (str[loop] == un)
		{
			if (!underline)
				mywattron(win -> win, A_UNDERLINE);
			else
				mywattroff(win -> win, A_UNDERLINE);

			underline = 1 - underline;
		}
		else
		{
			waddch(win -> win, str[loop]);
		}
	}

	if (inv)
		mywattroff(win -> win, A_REVERSE);

	if (underline)
		mywattroff(win -> win, A_UNDERLINE);
}

void create_win_border(int x, int y, int width, int height, const char *title, NEWWIN **bwin, NEWWIN **win, bool f1, bool with_blank_border)
{
	assert(x >= 0 && y >= 0);
	assert(x < max_x && y < max_y);
	assert(x + width <= max_x && y + height <= max_y);
	const char f1_for_help [] = " F1 for help ";

	bool wbb = with_blank_border;

        *bwin = create_window_xy(y + 0, x + 0, height + 2 + wbb * 2, width + 2 + wbb * 2);
        *win  = create_window_xy(y + 1 + wbb, x + 1 + wbb, height + 0, width + 0);

        mywattron((*bwin) -> win, A_REVERSE);
        box((*bwin) -> win, 0, 0);
        mywattroff((*bwin) -> win, A_REVERSE);

	mywattron((*bwin) -> win, A_STANDOUT);

	mvwprintw((*bwin) -> win, 0, 1, "[ %s ]", title);

	if (f1)
		mvwprintw((*bwin) -> win, (*bwin) -> nlines - 1, 2, "[ %s ]", f1_for_help);

	mywattroff((*bwin) -> win, A_STANDOUT);
}

void create_wb_popup(int width, int height, const char *title, NEWWIN **bwin, NEWWIN **win)
{
	create_win_border(max_x / 2 - width / 2, max_y / 2 - height / 2, width, height, title, bwin, win, false, true);
}

void mywattron(WINDOW *w, int a)
{
	if (a != A_BLINK && a != A_BOLD && a != A_NORMAL && a != A_REVERSE && a != A_STANDOUT && a != A_UNDERLINE)
		error_exit(false, "funny attributes: %d", a);

	wattron(w, a);
}

void mywattroff(WINDOW *w, int a)
{
	if (a != A_BLINK && a != A_BOLD && a != A_NORMAL && a != A_REVERSE && a != A_STANDOUT && a != A_UNDERLINE)
		error_exit(false, "funny attributes: %d", a);

	wattroff(w, a);
}

void reset_attributes(NEWWIN *win)
{
	wattrset(win -> win, A_NORMAL);
}

bool is_in_window(NEWWIN *win, int x, int y)
{
	return wenclose(win -> win, y, x);
}

bool right_mouse_button_clicked(void)
{
	MEVENT event;

	return getmouse(&event) == OK && (event.bstate & BUTTON3_CLICKED);
}
