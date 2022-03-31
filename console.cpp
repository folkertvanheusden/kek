#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>

#include "console.h"
#include "gen.h"
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
	for(size_t x=0; x<what.size(); x++)
		put_char_ll(what.at(x));
}

void console::operator()()
{
	D(fprintf(stderr, "Console thread started\n");)

	while(!*terminate) {
		int c = wait_for_char(500);

		if (c == -1)
			continue;

		if (c == 3)  // ^c
			*terminate = true;
		else if (c == 12)  // ^l
			refresh_virtual_terminal();
		else
			input_buffer.push_back(c);
	}

	D(fprintf(stderr, "Console thread terminating\n");)
}
