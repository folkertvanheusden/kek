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

void console_comm::set_panel_mode(const panel_mode_t pm)
{
}

int console_comm::wait_for_char_ll(const short timeout)
{
	for(short i=0; i<timeout / 10 && !stop_panel; i++) {
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
}

void console_comm::panel_update_thread()
{
	if (p_blinkenlights) {
		while(!stop_panel) {
			myusleep(1000000 / refreshrate);
			p_blinkenlights->push(b, running_flag);
		}
	}
}
