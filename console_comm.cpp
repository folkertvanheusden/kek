// (C) 2026 by Folkert van Heusden
// Released under MIT license

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "blinkenlights.h"
#include "bus.h"
#include "console_comm.h"
#include "cpu.h"
#if defined(BUILD_FOR_PICO2W)
#include "pico2w.h"
#elif defined(TEENSY4_1)
#include "teensy4_1.h"
#else
#include "comm.h"
#endif
#include "ddp.h"
#include "error.h"
#include "utils.h"


console_comm::console_comm(kek_event_t *const stop_event, comm *const io_port, const int t_width, const int t_height) :
	console(stop_event, t_width, t_height),
	io_port(io_port)
{
}

console_comm::~console_comm()
{
	stop_thread();
	delete io_port;
}

int console_comm::wait_for_char_ll(const int timeout)
{
	for(int i=0; i<timeout / 10 && !stop_panel; i++) {
		if (io_port->has_data())
			return io_port->get_byte();
		myusleep(10000);
	}

	return -1;
}

void console_comm::put_char_ll(const char c)
{
	io_port->send_data(reinterpret_cast<const uint8_t *>(&c), 1);
}

void console_comm::put_string_lf(const std::string & what)
{
	put_string(what);
	put_string("\r\n");
}

void console_comm::resize_terminal()
{
}

void console_comm::refresh_virtual_terminal()
{
	put_char_ll(12);  // form feed
	for(int row=0; row<t_height; row++)
		put_string_lf(std::string(screen_buffer[row], t_width).c_str());
}

void console_comm::panel_update_thread()
{
	while(*stop_event != EVENT_TERMINATE && !stop_panel) {
		myusleep(1'000'000 / refreshrate);
		if (p_blinkenlights)
			p_blinkenlights->push(b, running_flag);
		if (p_ddp)
			p_ddp->push(this, b, brightness);
		// teensy 4.1 does not have atomics, so exchange() won't compile
		if (do_test_panel) {
			do_test_panel = false;
			if (p_blinkenlights)
				p_blinkenlights->test();
			if (p_ddp)
				p_ddp->test();
		}
	}
}

void console_comm::ui_event_loop()
{
	while(*stop_event != EVENT_TERMINATE)
		myusleep(1'000'000 / 10);
}
