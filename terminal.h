// (C) 2018 by folkert@vanheusden.com, released under AGPL 3.0
#pragma once

#include <string>
#include <ncurses.h>
#include <panel.h>

typedef struct
{
	WINDOW *win;
	PANEL *pwin;

	unsigned nlines, ncols;
	int x, y;
} NEWWIN;

extern int default_colorpair, highlight_colorpair, meta_colorpair, error_colorpair, notice_colorpair, markerline_colorpair;
extern int max_y, max_x;

void wrong_key(void);
void color_on(NEWWIN *win, int pair);
void color_off(NEWWIN *win, int pair);
void mywattron(WINDOW *w, int a);
void mywattroff(WINDOW *w, int a);
void mywbkgd(NEWWIN *win, int pair);
void mydelwin(NEWWIN *win);
void mydoupdate();
void delete_window(NEWWIN *mywin);
NEWWIN * create_window(int n_lines, int n_colls);
NEWWIN * create_window_xy(int y_offset, int x_offset, int n_lines, int n_colls);
void limit_print(NEWWIN *win, int width, int y, int x, const char *format, ...);
void escape_print_xy(NEWWIN *win, int y, int x, const char *str);
void escape_print(NEWWIN *win, const char *str, const char reverse, const char underline);
void determine_terminal_size(void);
void create_win_border(int x, int y, int width, int height, const char *title, NEWWIN **bwin, NEWWIN **win, bool f1, bool with_blank_border = false);
void create_wb_popup(int width, int height, const char *title, NEWWIN **bwin, NEWWIN **win);
void initcol(void);
void apply_mouse_setting(void);
void init_ncurses(bool ignore_mouse);
void reset_attributes(NEWWIN *win);
bool is_in_window(NEWWIN *win, int x, int y);
bool right_mouse_button_clicked(void);
void display_markerline(NEWWIN *win, const char *msg);
void simple_marker(NEWWIN *win);

void estimate_popup_size(const std::string & in, int *const w, int *const h);
