// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#if defined(ESP32)
#include <Arduino.h>
#endif
#if IS_POSIX
#include <errno.h>
#include <termios.h>
#include <thread>
#endif
#include <cstring>
#include <unistd.h>

#include "bus.h"
#include "cpu.h"
#include "dc11.h"
#include "log.h"
#include "utils.h"


#define ESP32_UART UART_NUM_1

// this line is reserved for a serial port
constexpr const int serial_line = 3;

const char *const dc11_register_names[] { "RCSR", "RBUF", "TSCR", "TBUF" };

dc11::dc11(bus *const b, const std::vector<comm *> & comm_interfaces):
	b(b),
	comm_interfaces(comm_interfaces)
{
	connected.resize(comm_interfaces.size());

	// TODO move to begin()
	th = new std::thread(std::ref(*this));
}

dc11::~dc11()
{
	DOLOG(debug, false, "DC11 closing");

	stop_flag = true;

	if (th) {
		th->join();
		delete th;
	}
}

void dc11::show_state(console *const cnsl) const
{
	for(size_t i=0; i<comm_interfaces.size(); i++) {
		cnsl->put_string_lf(format("* LINE %zu", i + 1));

#if 0  // TODO
		if (i == serial_line) {
			cnsl->put_string_lf(format(" TTY thread running: %s", serial_thread_running ? "true": "false" ));
			cnsl->put_string_lf(format(" TTY enabled: %s", serial_enabled ? "true": "false" ));
		}
		else {
			if (pfds[dc11_n_lines + i].fd != INVALID_SOCKET)
				cnsl->put_string_lf(" Connected to: " + get_endpoint_name(pfds[dc11_n_lines + i].fd));
		}
#endif

		std::unique_lock<std::mutex> lck(input_lock[i]);
		cnsl->put_string_lf(format(" Characters in buffer: %zu", recv_buffers[i].size()));

		cnsl->put_string_lf(format(" RX interrupt enabled: %s", is_rx_interrupt_enabled(i) ? "true": "false" ));
		cnsl->put_string_lf(format(" TX interrupt enabled: %s", is_tx_interrupt_enabled(i) ? "true": "false" ));
	}
}

void dc11::test_port(const size_t nr, const std::string & txt) const
{
	DOLOG(info, false, "DC11 test line %zu", nr);

	comm_interfaces.at(nr)->send_data(reinterpret_cast<const uint8_t *>(txt.c_str()), txt.size());
}

void dc11::test_ports(const std::string & txt) const
{
	for(size_t i=0; i<comm_interfaces.size(); i++)
		test_port(i, txt);
}

void dc11::trigger_interrupt(const int line_nr, const bool is_tx)
{
	TRACE("DC11: interrupt for line %d, %s", line_nr, is_tx ? "TX" : "RX");

	b->getCpu()->queue_interrupt(5, 0300 + line_nr * 010 + 4 * is_tx);
}

void dc11::operator()()
{
	set_thread_name("kek:DC11");

	DOLOG(info, true, "DC11 thread started");

	while(!stop_flag) {
		myusleep(5000);  // TODO replace polling

		for(size_t line_nr=0; line_nr<comm_interfaces.size(); line_nr++) {
			// (dis-)connected?
			bool is_connected = comm_interfaces.at(line_nr)->is_connected();

			if (is_connected != connected[line_nr]) {
				connected[line_nr] = is_connected;

				if (is_connected)
					registers[line_nr * 4 + 0] |= 0160000;  // "ERROR", RING INDICATOR, CARRIER TRANSITION
				else
					registers[line_nr * 4 + 0] |= 0120000;  // "ERROR", CARRIER TRANSITION

				if (is_rx_interrupt_enabled(line_nr))
					trigger_interrupt(line_nr, false);
			}

			// receive data
			bool have_data = false;
			while(comm_interfaces.at(line_nr)->has_data()) {
				uint8_t buffer = comm_interfaces.at(line_nr)->get_byte();

				std::unique_lock<std::mutex> lck(input_lock[line_nr]);
				recv_buffers[line_nr].push_back(char(buffer));

				have_data = true;
			}

			if (have_data) {
				registers[line_nr * 4 + 0] |= 128;  // DONE: bit 7

				if (is_rx_interrupt_enabled(line_nr))
					trigger_interrupt(line_nr, false);
			}
		}
	}

	DOLOG(info, true, "DC11 thread terminating");
}

void dc11::reset()
{
}

bool dc11::is_rx_interrupt_enabled(const int line_nr) const
{
	return !!(registers[line_nr * 4 + 0] & 64);
}

bool dc11::is_tx_interrupt_enabled(const int line_nr) const
{
	return !!(registers[line_nr * 4 + 2] & 64);
}

uint8_t dc11::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t dc11::read_word(const uint16_t addr)
{
	int      reg     = (addr - DC11_BASE) / 2;
	int      line_nr = reg / 4;
	int      sub_reg = reg & 3;

	std::unique_lock<std::mutex> lck(input_lock[line_nr]);

	uint16_t vtemp   = registers[reg];

	if (sub_reg == 0) {  // receive status
		// emulate DTR, CTS & READY
		registers[line_nr * 4 + 0] &= ~1;  // DTR: bit 0  [RCSR]
		registers[line_nr * 4 + 0] &= ~4;  // CD : bit 2

		if (comm_interfaces.at(line_nr)->is_connected()) {
			registers[line_nr * 4 + 0] |= 1;
			registers[line_nr * 4 + 0] |= 4;
		}

		vtemp = registers[line_nr * 4 + 0];

		// clear error(s)
		registers[line_nr * 4 + 0] &= ~0160000;
	}
	else if (sub_reg == 1) {  // read data register
		TRACE("DC11: %zu characters in buffer for line %d", recv_buffers[line_nr].size(), line_nr);

		// get oldest byte in buffer
		if (recv_buffers[line_nr].empty() == false) {
			vtemp = *recv_buffers[line_nr].begin();

			// parity check
			registers[line_nr * 4 + 0] &= ~(1 << 5);
			registers[line_nr * 4 + 0] |= parity(vtemp) << 5;

			recv_buffers[line_nr].erase(recv_buffers[line_nr].begin());

			// still data in buffer? generate interrupt
			if (recv_buffers[line_nr].empty() == false) {
				registers[line_nr * 4 + 0] |= 128;  // DONE: bit 7

				if (is_rx_interrupt_enabled(line_nr))
					trigger_interrupt(line_nr, false);
			}
		}
	}
	else if (sub_reg == 2) {  // transmit status
		registers[line_nr * 4 + 2] &= ~2;  // CTS: bit 1  [TSCR]
		registers[line_nr * 4 + 2] &= ~128;  // READY: bit 7

		if (comm_interfaces.at(line_nr)->is_connected()) {
			registers[line_nr * 4 + 2] |= 2;
			registers[line_nr * 4 + 2] |= 128;
		}

		vtemp = registers[line_nr * 4 + 2];
	}

	TRACE("DC11: read register %06o (\"%s\", %d line %d): %06o", addr, dc11_register_names[sub_reg], sub_reg, line_nr, vtemp);

	return vtemp;
}

void dc11::write_byte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - DC11_BASE) / 2];
	
	if (addr & 1) {
		vtemp &= ~0xff00;
		vtemp |= v << 8;
	}
	else {
		vtemp &= ~0x00ff;
		vtemp |= v;
	}

	write_word(addr, vtemp);
}

void dc11::write_word(const uint16_t addr, const uint16_t v)
{
	int reg     = (addr - DC11_BASE) / 2;
	int line_nr = reg / 4;
	int sub_reg = reg & 3;

	std::unique_lock<std::mutex> lck(input_lock[line_nr]);

	TRACE("DC11: write register %06o (\"%s\", %d line_nr %d) to %06o", addr, dc11_register_names[sub_reg], sub_reg, line_nr, v);

	if (sub_reg == 3) {  // transmit buffer
		char c = v & 127;  // strip parity

		if (c <= 32 || c >= 127)
			TRACE("DC11: transmit [%d] on line %d", c, line_nr);
		else
			TRACE("DC11: transmit %c on line %d", c, line_nr);

		comm_interfaces.at(line_nr)->send_data(reinterpret_cast<const uint8_t *>(&c), 1);

		if (is_tx_interrupt_enabled(line_nr))
			trigger_interrupt(line_nr, true);
	}

	registers[reg] = v;
}
