#include <chrono>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>

#include "console.h"
#include "gen.h"
#include "utils.h"


console::console(std::atomic_uint32_t *const stop_event, bus *const b) :
	stop_event(stop_event),
	b(b)
{
	memset(screen_buffer, ' ', sizeof screen_buffer);
}

console::~console()
{
}

void console::start_thread()
{
	stop_thread_flag = false;

#if !defined(ESP32)
	th = new std::thread(std::ref(*this));
#endif
}

void console::stop_thread()
{
	if (th) {
		stop_thread_flag = true;

		th->join();
		delete th;

		th = nullptr;
	}
}

bool console::poll_char()
{
	std::unique_lock<std::mutex> lck(input_lock);

	return input_buffer.empty() == false;
}

int console::get_char()
{
	std::unique_lock<std::mutex> lck(input_lock);

	if (input_buffer.empty())
		return -1;

	char c = input_buffer.at(0);

	input_buffer.erase(input_buffer.begin() + 0);

	return c;
}

int console::wait_char(const int timeout_ms)
{
	std::unique_lock<std::mutex> lck(input_lock);

	using namespace std::chrono_literals;

	if (input_buffer.empty() == false || have_data.wait_for(lck, timeout_ms * 1ms) == std::cv_status::no_timeout) {
		if (input_buffer.empty() == false) {
			int c = input_buffer.at(0);

			input_buffer.erase(input_buffer.begin() + 0);

			return c;
		}
	}

	return -1;
}

void console::flush_input()
{
	input_buffer.clear();
}

void console::emit_backspace()
{
	put_char(8);
	put_char(' ');
	put_char(8);
}

std::string console::read_line(const std::string & prompt)
{
	put_string(prompt);
	put_string(">");

	std::string str;

	for(;;) {
		char c = wait_char(500);

		if (*stop_event == EVENT_TERMINATE)
			return "";

		if (c == -1)
			continue;

		if (c == 13 || c == 10)
			break;

		if (c == 8 || c == 127) {  // backspace
			if (!str.empty()) {
				str = str.substr(0, str.size() - 1);

				emit_backspace();
			}
		}
		else if (c == 21) {  // ^u
			for(size_t i=0; i<str.size(); i++)
				emit_backspace();

			str.clear();
		}
		else if (c >= 32 && c < 127) {
			str += c;

			put_char(c);
		}
	}

	put_string_lf("");

	return str;
}

void console::debug(const std::string fmt, ...)
{
	char *buffer = nullptr;

        va_list ap;
        va_start(ap, fmt);

        vasprintf(&buffer, fmt.c_str(), ap);

        va_end(ap);

	put_string_lf(buffer);

	free(buffer);
}

void console::put_char(const char c)
{
	put_char_ll(c);

	if (c == 0) {
	}
	else if (c == 13)
		tx = 0;
	else if (c == 10) {
		if (debug_buffer.empty() == false) {
			D(fprintf(stderr, "TTY: %s\n", debug_buffer.c_str());)

			debug_buffer.clear();
		}

		ty++;
	}
	else if (c == 8) {  // backspace
		if (tx > 0)
			tx--;
	}
	else {
		screen_buffer[ty][tx++] = c;

		if (tx == t_width) {
			tx = 0;

			ty++;
		}

		if (debug_buffer.size() < 4096)
			debug_buffer += c;
	}

	if (ty == t_height) {
		memmove(&screen_buffer[0], &screen_buffer[1], sizeof(char) * t_width * (t_height - 1));

		ty--;

		memset(screen_buffer[t_height - 1], ' ', t_width);
	}
}

void console::put_string(const std::string & what)
{
	for(size_t x=0; x<what.size(); x++)
		put_char(what.at(x));
}

void console::operator()()
{
	D(fprintf(stderr, "Console thread started\n");)

	set_thread_name("kek::console");

	while(*stop_event != EVENT_TERMINATE && !stop_thread_flag) {
		int c = wait_for_char_ll(100);

		if (c == -1)
			continue;

		bool running_flag = *get_running_flag();

//		printf("%d %d\n", running_flag, c);

		if (running_flag == false && c == 3)  // ^c
			*stop_event = EVENT_TERMINATE;
		else if (running_flag == true && c == 5)  // ^e
			*stop_event = EVENT_INTERRUPT;
		else if (running_flag == false && c == 12)  // ^l
			refresh_virtual_terminal();
		else {
			input_buffer.push_back(c);

			have_data.notify_all();
		}
	}

	D(fprintf(stderr, "Console thread terminating\n");)
}
