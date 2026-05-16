// (C) 2026 by Folkert van Heusden
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


const char *const dc11_register_names[] { "RCSR", "RBUF", "TSCR", "TBUF" };

#if defined(ESP32) || defined(FREERTOS)
static void thread_wrapper_dc11(void *p)
{
	dc11 *const dc11_ = reinterpret_cast<dc11 *>(p);

	dc11_->operator()();
}
#endif

dc11::dc11(bus *const b, comm_io *const io_channels):
	b(b),
	io_channels(io_channels)  // FIXME must be 4 elements
{
	connected.resize(4);
}

dc11::~dc11()
{
	DOLOG(debug, false, "DC11 closing");

	stop_flag = true;

	if (th) {
		th->join();
		delete th;
	}

	delete io_channels;
}

void dc11::show_state(console *const cnsl) const
{
	for(size_t i=0; i<dc11_n_lines; i++) {
		cnsl->put_string_lf(format("* LINE %zu", i + 1));

		cnsl->put_string_lf(" identifier: " + io_channels->get_identifier(i));

		my_unique_lock lck(&input_lock[i]);
		cnsl->put_string_lf(format(" Characters in buffer: %zu", recv_buffers[i].size()));

		cnsl->put_string_lf(format(" RX interrupt enabled: %s", is_rx_interrupt_enabled(i) ? "true": "false" ));
		cnsl->put_string_lf(format(" TX interrupt enabled: %s", is_tx_interrupt_enabled(i) ? "true": "false" ));
	}
}

bool dc11::begin()
{
#if defined(ESP32) || defined(FREERTOS)
	xTaskCreate(&thread_wrapper_dc11, "dc11", 3072, this, 1, nullptr);
#else
	th = new std::thread(std::ref(*this));
#endif

	return true;
}

void dc11::test_port(const size_t nr, const std::string & txt) const
{
	DOLOG(info, false, "DC11 test line %zu", nr);

	io_channels->send_data(nr, reinterpret_cast<const uint8_t *>(txt.c_str()), txt.size());
}

void dc11::test_ports(const std::string & txt) const
{
	for(size_t i=0; i<dc11_n_lines; i++)
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
		myusleep(10000);  // TODO replace polling

		for(size_t line_nr=0; line_nr<dc11_n_lines; line_nr++) {
			my_unique_lock lck(&input_lock[line_nr]);

			// (dis-)connected?
			bool is_connected  = io_channels->is_connected(line_nr);
			if (is_connected != connected[line_nr]) {
				DOLOG(debug, false, "DC11 line %d state changed to %d", line_nr, is_connected);
#if defined(ESP32)
				Serial.printf("DC11 line %d state changed to %d\r\n", line_nr, is_connected);
#endif

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
			while(io_channels->has_data(line_nr)) {
				uint8_t buffer = io_channels->get_byte(line_nr);
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

void dc11::reset(const bool reset)
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

	my_unique_lock lck(&input_lock[line_nr]);

	uint16_t vtemp   = registers[reg];

	if (sub_reg == 0) {  // receive status
		// emulate DTR, CTS & READY
		registers[line_nr * 4 + 0] &= ~1;  // DTR: bit 0  [RCSR]
		registers[line_nr * 4 + 0] &= ~4;  // CD : bit 2

		if (connected[line_nr]) {
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

		if (io_channels->is_connected(line_nr)) {
			registers[line_nr * 4 + 2] |= 2;
			registers[line_nr * 4 + 2] |= 128;
		}

		vtemp = registers[line_nr * 4 + 2];
	}

	TRACE("DC11: read register %06o (\"%s\", %d line %d): %06o", addr, dc11_register_names[sub_reg], sub_reg, line_nr, vtemp);

	return vtemp;
}

// FIXME locking
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

	my_unique_lock lck(&input_lock[line_nr]);

	TRACE("DC11: write register %06o (\"%s\", %d line_nr %d) to %06o", addr, dc11_register_names[sub_reg], sub_reg, line_nr, v);

	if (sub_reg == 3) {  // transmit buffer
		char c = v & 127;  // strip parity

		if (c <= 32 || c >= 127)
			TRACE("DC11: transmit [%d] on line %d", c, line_nr);
		else
			TRACE("DC11: transmit %c on line %d", c, line_nr);

		io_channels->send_data(line_nr, reinterpret_cast<const uint8_t *>(&c), 1);

		if (is_tx_interrupt_enabled(line_nr))
			trigger_interrupt(line_nr, true);
	}

	registers[reg] = v;
}

JsonDocument dc11::serialize() const
{
	JsonDocument j;
	j["interfaces"] = io_channels->serialize();

	for(int regnr=0; regnr<4 * dc11_n_lines; regnr++)
		j[format("register-%d", regnr)] = registers[regnr];

	return j;
}

dc11 *dc11::deserialize(const JsonVariantConst j, bus *const b)
{
	comm_io *io_channels = comm_io::deserialize(j["interfaces"], b);

	dc11 *r = new dc11(b, io_channels);
	r->begin();

	for(int regnr=0; regnr<4 * dc11_n_lines; regnr++)
		r->registers[regnr] = j[format("register-%d", regnr)];

	return r;
}
