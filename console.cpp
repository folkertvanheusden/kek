// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <cassert>
#include <chrono>
#include <optional>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>

#include "bus.h"
#include "console.h"
#include "cpu.h"
#include "log.h"
#include "tty.h"
#include "utils.h"


#if defined(BUILD_FOR_PICO2W) || defined(FREERTOS)
void thread_wrapper_console(void *p)
{
	console *const c = reinterpret_cast<console *>(p);

	c->operator()();
}
#endif

console::console(kek_event_t *const stop_event, const int t_width, const int t_height) :
	stop_event(stop_event),
	t_width(t_width),
	t_height(t_height)
{
	screen_buffer = new char[t_width * t_height]();
}

console::~console()
{
	// done as well in subclasses but also here to
	// stop lgtm.com complaining about it
	stop_thread();

	delete [] screen_buffer;
}

void console::begin()
{
}

void console::start_thread() 
{
	assert(b);

	stop_thread_flag = false;

#if defined(BUILD_FOR_PICO2W) || defined(FREERTOS)
	xTaskCreate(&thread_wrapper_console, "console", 1024, this, 1, nullptr);
#else
	th_kb = new std::thread(std::ref(*this));
#endif
}

void console::stop_thread()
{
#if !defined(ESP32) && !defined(BUILD_FOR_PICO2W) && !defined(FREERTOS)
	if (th_kb) {
		stop_thread_flag = true;

		th_kb->join();
		delete th_kb;

		th_kb = nullptr;
	}
#endif
}

bool console::poll_char()
{
	return input_buffer.is_empty() == false;
}

int console::get_char()
{
	auto c = wait_char(100);
	if (c.has_value() == false)
		return -1;
	return c.value();
}

std::optional<int> console::wait_char(const int timeout_ms)
{
	auto c = input_buffer.pop(timeout_ms);
	if (c.has_value() == false)
		return { };
	return c.value();
}

void console::unget_char(const char c)
{
	input_buffer.push_front(c);
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

std::string console::read_line(const std::string & prompt, const explode_func_t & ef)
{
	put_string(prompt + ">");

	while(edit_lines_hist.size() >= n_edit_lines_hist)
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

			// TODO: handle with number-prefix
			if (c == 'A') {  // up
				if (line_nr > 0)
					line_nr--;
			}
			else if (c == 'B') {  // down
				if (line_nr < edit_lines_hist.size() - 1)
					line_nr++;
			}
			else {
				continue;
			}

			for(size_t i=0; i<edit_lines_hist.at(line_nr).size(); i++)
				put_char(edit_lines_hist.at(line_nr).at(i));

			continue;
		}

		if (c.value() == 9 && ef != nullptr) {
			auto exploded = ef(this, edit_lines_hist.at(line_nr));
			if (exploded.has_value()) {
				auto & to_add = exploded.value();
				edit_lines_hist.at(line_nr) += to_add;
				put_string(to_add);
			}
			else {
				put_string(prompt + ">");
				put_string(edit_lines_hist.at(line_nr));
			}
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

	if (line_nr != edit_lines_hist.size() - 1)
		edit_lines_hist.push_back(edit_lines_hist.at(line_nr));

	return edit_lines_hist.at(line_nr);
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
		if (debug_buffer.empty() == false && is_terminal_set() == false) {
			DOLOG(log_ss::LS_COMM, "TTY: %s", debug_buffer.c_str());
			debug_buffer.clear();
		}

		if (timestamps) {
			char     buffer[32] { };
			uint64_t timestamp = get_us() - start_ts;
			uint64_t seconds   = timestamp / 1000000;

			int len = snprintf(buffer, sizeof buffer, "%02d:%02d:%02d.%06d ",
					int(seconds / 3600),
					int((seconds / 60) % 60),
					int(seconds % 60),
					int(timestamp % 1000000));

			for(int i=0; i<len; i++)
				put_char_ll(buffer[i]);
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
	my_unique_lock lck(&put_string_lock);
	for(size_t x=0; x<what.size(); x++)
		put_char(what.at(x));
}

void console::operator()()
{
	DOLOG(log_ss::LS_GENERIC, "Console thread started");
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
			input_buffer.push(c);
			if (have_data_cb_notifier)
				have_data_cb_notifier->notify_rx();
		}
	}

	DOLOG(log_ss::LS_GENERIC, "Console thread terminating");
}

void console::set_blinkenlights_panel(blinkenlights *const p_blinkenlights)
{
	this->p_blinkenlights = p_blinkenlights;
}

void console::set_ddp_panel(ddp *const p_ddp)
{
	this->p_ddp = p_ddp;
}

void console::set_LED_state(const bool)
{
}

void console::pulse_LED()
{
}

void console::set_panel_mode(const panel_mode_t pm)
{
	panel_mode = pm;
}

static void add_pixel(std::vector<std::tuple<uint8_t, uint8_t, uint8_t> > & to, const uint8_t color[])
{
	to.push_back({ color[0], color[1], color[2] });
}

void console::generate_panel_colors(std::vector<std::tuple<uint8_t, uint8_t, uint8_t> > & to, const size_t n_leds, bus *const b, cpu *const c, const uint8_t brightness)
{
	const uint8_t black  [] = { 0,          0,          0          };
	const uint8_t magenta[] = { brightness, 0,          brightness };
	const uint8_t red    [] = { brightness, 0,          0          };
	const uint8_t green  [] = { 0,          brightness, 0          };
	const uint8_t blue   [] = { 0,          0,          brightness };
	const uint8_t yellow [] = { brightness, brightness, 0          };
	const uint8_t white  [] = { brightness, brightness, brightness };
	const uint8_t *const run_mode_led_color[4] = { red, yellow, blue, green };

	try {
		// note that these are approximately as there's no mutex on the emulation
		uint16_t       current_PSW   = c->getPSW();
		int            run_mode      = current_PSW >> 14;
		const uint8_t *led_color     = run_mode_led_color[run_mode];
		uint16_t       current_PC    = c->getPC();

		switch(panel_mode) {
			case PM_BITS: {
					      memory_addresses_t rc            = b->getMMU()->calculate_physical_address(run_mode, current_PC);
					      auto               current_instr = b->peek_word(run_mode, current_PC);

					      for(uint8_t b=0; b<22; b++)
						      add_pixel(to, rc.physical_instruction & (1 << b) ? led_color : black);

					      for(uint8_t b=0; b<3; b++)
						      add_pixel(to, rc.apf ? yellow : black);

					      add_pixel(to, rc.physical_instruction_is_psw | rc.physical_data_is_psw ? blue : black);

					      add_pixel(to, b->getMMU()->is_enabled() ? white : black);

					      add_pixel(to, b->getMMU()->getMMR3() & 7 ? white : black);

					      for(uint8_t b=0; b<16; b++)
						      add_pixel(to, current_PSW & (1l << b) ? magenta : black);

					      if (current_instr.has_value()) {
						      for(uint8_t b=0; b<16; b++)
							      add_pixel(to, current_instr.value() & (1l << b) ? red : black);
					      }
					      else {
						      for(uint8_t b=0; b<16; b++)
							      add_pixel(to, black);
					      }

					      add_pixel(to, running_flag             ? white : black);

					      add_pixel(to, disk_read_activity_flag  ? blue  : black);
					      disk_read_activity_flag  = false;
					      add_pixel(to, disk_write_activity_flag ? blue  : black);
					      disk_write_activity_flag = false;

					      add_pixel(to, network_activity_flag    ? yellow: black);
					      network_activity_flag    = false;
				      }
				      break;

			case PM_ADDRESS1:
				to.resize(n_leds);
				to[current_PC * to.size() / 65536] = { led_color[0], led_color[1], led_color[2] };
				break;

			case PM_ADDRESS2:
				to.resize(n_leds);
				to[current_PC % to.size()] = { led_color[0], led_color[1], led_color[2] };
				break;
		}
	}
	catch(const std::exception & e) {
		put_string_lf(format("Exception in generate_panel_colors: %s", e.what()));
	}
	catch(const int e) {
		put_string_lf(format("Exception in generate_panel_colors: %d", e));
	}
	catch(...) {
		put_string_lf("Unknown exception in generate_panel_colors");
	}
}
