#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>

#include "console.h"
#include "utils.h"


console::console(std::atomic_bool *const terminate, bus *const b) :
	terminate(terminate),
	b(b)
{
	memset(screen_buffer, ' ', sizeof screen_buffer);
}

console::~console()
{
}

bool console::poll_char()
{
	return input_buffer.empty() == false;
}

uint8_t console::get_char()
{
	if (input_buffer.empty())
		return 0x00;

	char c = input_buffer.at(0);

	input_buffer.erase(input_buffer.begin() + 0);

	return c;
}

void console::debug(const std::string fmt, ...)
{
	char *buffer = nullptr;

        va_list ap;
        va_start(ap, fmt);

        int len = vasprintf(&buffer, fmt.c_str(), ap);

        va_end(ap);

	for(int i=0; i<len; i++)
		put_char(buffer[i]);

	free(buffer);
}

void console::put_char(const char c)
{
	put_char_ll(c);

	if (c == 0) {
	}
	else if (c == 13)
		tx = 0;
	else if (c == 10)
		ty++;
	else {
		screen_buffer[ty][tx++] = c;

		if (tx == t_width) {
			tx = 0;

			ty++;
		}
	}

	if (ty == t_height) {
		memmove(&screen_buffer[0], &screen_buffer[1], sizeof(char) * t_width * (t_height - 1));

		ty--;

		memset(screen_buffer[t_height - 1], ' ', t_width);
	}
}

void console::put_string_ll(const std::string & what)
{
	for(int x=0; x<what.size(); x++)
		put_char_ll(what.at(x));
}

void console::operator()()
{
	debug("Console thread started");

	while(!*terminate) {
		int c = wait_for_char(500);

		if (c == -1)
			continue;

		if (c == 3)  // ^c
			*terminate = true;
		else if (c == 12) {  // ^l
			put_string_ll(format("\033[2J\033[?7l"));

			fprintf(stderr, "%d %d\n", tx, ty);

			for(int row=0; row<t_height; row++) {
				put_string_ll(format("\033[%dH", row + 1));

				put_string_ll(std::string(screen_buffer[row], t_width));
			}

			put_string_ll(format("\033[%d;%dH\033[?7h", ty + 1, tx + 1));
		}
		else {
			input_buffer.push_back(c);
		}
	}

	debug("Console thread terminating");
}
