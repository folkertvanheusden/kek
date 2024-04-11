// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <chrono>
#include <optional>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>

#include "console.h"
#include "gen.h"
#include "log.h"
#include "utils.h"

#if defined(BUILD_FOR_RP2040)
#include "rp2040.h"

void thread_wrapper_console(void *p)
{
	console *const consolel = reinterpret_cast<console *>(p);

	consolel->operator()();
}
#endif

console::console(std::atomic_uint32_t *const stop_event, bus *const b, const int t_width, const int t_height) :
	stop_event(stop_event),
	b(b),
	t_width(t_width),
	t_height(t_height)
{
	screen_buffer = new char[t_width * t_height]();

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(input_lock);  // initialize
#endif
}

console::~console()
{
	// done as well in subclasses but also here to
	// stop lgtm.com complaining about it
	stop_thread();

	delete [] screen_buffer;
}

void console::start_thread() 
{
	stop_thread_flag = false;

#if defined(BUILD_FOR_RP2040)
	xTaskCreate(&thread_wrapper_console, "console", 2048, this, 1, nullptr);
#else
	th = new std::thread(std::ref(*this));
#endif
}

void console::stop_thread()
{
#if !defined(ESP32) && !defined(BUILD_FOR_RP2040)
	if (th) {
		stop_thread_flag = true;

		th->join();
		delete th;

		th = nullptr;
	}
#endif
}

bool console::poll_char()
{
#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(input_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(input_lock);
#endif

	bool rc = input_buffer.empty() == false;

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(input_lock);
#endif

	return rc;
}

int console::get_char()
{
#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(input_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(input_lock);
#endif

	char c = -1;

	if (input_buffer.empty() == false) {
		c = input_buffer.at(0);

		input_buffer.erase(input_buffer.begin() + 0);
	}

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(input_lock);
#endif
	return c;
}

std::optional<char> console::wait_char(const int timeout_ms)
{
#if defined(BUILD_FOR_RP2040)
	uint8_t rc = 0;
	if (xQueueReceive(have_data, &rc, timeout_ms / portTICK_PERIOD_MS) == pdFALSE || rc == 0)
		return { };

	std::optional<char> c { };

	xSemaphoreTake(input_lock, portMAX_DELAY);

	if (input_buffer.empty() == false) {
		c = input_buffer.at(0);

		input_buffer.erase(input_buffer.begin() + 0);
	}

	xSemaphoreGive(input_lock);

	return c;
#else
	std::unique_lock<std::mutex> lck(input_lock);

	using namespace std::chrono_literals;

	if (input_buffer.empty() == false || have_data.wait_for(lck, timeout_ms * 1ms) == std::cv_status::no_timeout) {
		if (input_buffer.empty() == false) {
			int c = input_buffer.at(0);

			input_buffer.erase(input_buffer.begin() + 0);

			return c;
		}
	}

	return { };
#endif
}

void console::flush_input()
{
#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(input_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(input_lock);
#endif

	input_buffer.clear();

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(input_lock);
#endif
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

	if (edit_lines_hist.empty() == false)
		edit_lines_hist.erase(edit_lines_hist.begin());

	while(edit_lines_hist.size() < n_edit_lines_hist)
		edit_lines_hist.push_back("");

	size_t line_nr = edit_lines_hist.size() - 1;
	bool   escape  = false;

	for(;;) {
		auto c = wait_char(250);

		if (*stop_event == EVENT_TERMINATE)
			return "";

		if (c.has_value() == false)
			continue;

		if (c == 27) {
			escape = true;
			continue;
		}

		if (escape) {
			if (c == '[')
				continue;

			escape = false;

			for(size_t i=0; i<edit_lines_hist.at(line_nr).size(); i++)
				emit_backspace();

			if (c == 'A') {  // up
				if (line_nr > 0)
					line_nr--;
			}
			else if (c == 'B') {  // down
				if (line_nr < edit_lines_hist.size() - 1)
					line_nr++;
			}
			else {
				printf("[%c]\n", c);
				continue;
			}

			for(size_t i=0; i<edit_lines_hist.at(line_nr).size(); i++)
				put_char(edit_lines_hist.at(line_nr).at(i));

			continue;
		}

		if (c.value() == 13 || c.value() == 10)
			break;

		if (c.value() == 8 || c.value() == 127) {  // backspace
			if (!edit_lines_hist.at(line_nr).empty()) {
				edit_lines_hist.at(line_nr) = edit_lines_hist.at(line_nr).substr(0, edit_lines_hist.at(line_nr).size() - 1);

				emit_backspace();
			}
		}
		else if (c.value() == 21) {  // ^u
			for(size_t i=0; i<edit_lines_hist.at(line_nr).size(); i++)
				emit_backspace();

			edit_lines_hist.at(line_nr).clear();
		}
		else if (c.value() == 23) {  // ^w
			while(edit_lines_hist.at(line_nr).empty() == false) {
				edit_lines_hist.at(line_nr) = edit_lines_hist.at(line_nr).substr(0, edit_lines_hist.at(line_nr).size() - 1);
				emit_backspace();

				if (edit_lines_hist.at(line_nr).empty() == false && edit_lines_hist.at(line_nr).at(edit_lines_hist.at(line_nr).size() - 1) == ' ')
					break;
			}
		}
		else if (c.value() >= 32) {
			edit_lines_hist.at(line_nr) += c.value();

			put_char(c.value());
		}
	}

	put_string_lf("");

	return edit_lines_hist.at(line_nr);
}

void console::debug(const std::string fmt, ...)
{
#if defined(BUILD_FOR_RP2040)
	char buffer[128];
        va_list ap;

        va_start(ap, fmt);
	vsnprintf(buffer, sizeof buffer, fmt.c_str(), ap);
	va_end(ap);

	put_string_lf(buffer);
#else
	char *buffer = nullptr;

        va_list ap;
        va_start(ap, fmt);

        vasprintf(&buffer, fmt.c_str(), ap);

        va_end(ap);

	put_string_lf(buffer);

	free(buffer);
#endif
}

void console::put_char(const char c)
{
	put_char_ll(c);

	if (c == 0) {
		// ignore these
	}
	else if (c == 13)
		tx = 0;
	else if (c == 10) {
		if (debug_buffer.empty() == false) {
			DOLOG(::debug, true, "TTY: %s", debug_buffer.c_str());

			debug_buffer.clear();
		}

		ty++;
	}
	else if (c == 8) {  // backspace
		if (tx > 0)
			tx--;
	}
	else {
		screen_buffer[ty * t_width + tx++] = c;

		if (tx == t_width) {
			tx = 0;

			ty++;
		}

		if (debug_buffer.size() < 4096)
			debug_buffer += c;
	}

	if (ty == t_height) {
		memmove(&screen_buffer[0 * t_width], &screen_buffer[1 * t_width], sizeof(char) * t_width * (t_height - 1));

		ty--;

		memset(&screen_buffer[(t_height - 1) * t_width], ' ', t_width);
	}
}

void console::put_string(const std::string & what)
{
	for(size_t x=0; x<what.size(); x++)
		put_char(what.at(x));
}

void console::operator()()
{
	DOLOG(::info, true, "Console thread started");

	set_thread_name("kek::console");

	while(*stop_event != EVENT_TERMINATE && !stop_thread_flag) {
		int c = wait_for_char_ll(500);
		if (c == -1)
			continue;

		bool running_flag = *get_running_flag();

		if (running_flag == false && c == 3)  // ^c
			*stop_event = EVENT_TERMINATE;
		else if (running_flag == true && c == 5)  // ^e
			*stop_event = EVENT_INTERRUPT;
		else if (running_flag == false && c == 12)  // ^l
			refresh_virtual_terminal();
		else {
#if defined(BUILD_FOR_RP2040)
			xSemaphoreTake(input_lock, portMAX_DELAY);
#else
			std::unique_lock<std::mutex> lck(input_lock);
#endif

			input_buffer.push_back(c);

#if defined(BUILD_FOR_RP2040)
			xSemaphoreGive(input_lock);

			uint8_t value = 1;
			if (xQueueSend(have_data, &value, portMAX_DELAY) == pdFALSE)
				Serial.println("xQueueSend failed");
#else
			have_data.notify_all();
#endif
		}
	}

	DOLOG(::info, true, "Console thread terminating");
}
