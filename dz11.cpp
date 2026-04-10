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
#include "dz11.h"
#include "log.h"
#include "utils.h"


#define ESP32_UART UART_NUM_1

// this line is reserved for a serial port
constexpr const int serial_line = 7;

const char *const dz11_register_names[] { "R0_CSR", "R2_RBUF_LPR", "R4_TCR", "R6_MSR_TDR" };

dz11::dz11(bus *const b, const std::vector<comm *> & comm_interfaces):
	b(b),
	comm_interfaces(comm_interfaces)
{
	connected.resize(sizeof comm_interfaces);
}

dz11::~dz11()
{
	DOLOG(debug, false, "DZ11 closing");

	stop_flag = true;

	if (th) {
		th->join();
		delete th;
	}

	for(auto & c : comm_interfaces) {
		DOLOG(debug, false, "Stopping %s", c->get_identifier().c_str());
		delete c;
	}
}

void dz11::show_state(console *const cnsl) const
{
	for(size_t i=0; i<comm_interfaces.size(); i++) {
		cnsl->put_string_lf(format("* LINE %zu", i + 1));

		std::unique_lock<std::mutex> lck(input_lock);
		cnsl->put_string_lf(format(" Characters in buffer: %zu", recv_buffers[i].size()));
	}

	cnsl->put_string_lf(format(" RX interrupt enabled: %s", is_rx_interrupt_enabled() ? "true": "false" ));
	cnsl->put_string_lf(format(" TX interrupt enabled: %s", is_tx_interrupt_enabled() ? "true": "false" ));
}

bool dz11::begin()
{
	th = new std::thread(std::ref(*this));

	return true;
}

void dz11::test_port(const size_t nr, const std::string & txt) const
{
	DOLOG(info, false, "DZ11 test line %zu", nr);

	comm_interfaces.at(nr)->send_data(reinterpret_cast<const uint8_t *>(txt.c_str()), txt.size());
}

void dz11::test_ports(const std::string & txt) const
{
	for(size_t i=0; i<comm_interfaces.size(); i++)
		test_port(i, txt);
}

void dz11::trigger_interrupt(const bool is_tx)
{
	TRACE("DZ11: %s interrupt", is_tx ? "TX" : "RX");
	b->getCpu()->queue_interrupt(5, is_tx ? DZ11_INTERRUPT_VECTOR_TX : DZ11_INTERRUPT_VECTOR_RX);
}

void dz11::operator()()
{
	set_thread_name("kek:DZ11");

	DOLOG(info, true, "DZ11 thread started");

	while(!stop_flag) {
		myusleep(10000);  // TODO replace polling

		for(size_t line_nr=0; line_nr<comm_interfaces.size(); line_nr++) {
			std::unique_lock<std::mutex> lck(input_lock);

			// (dis-)connected?
			bool is_connected = comm_interfaces.at(line_nr)->is_connected();

			if (is_connected != connected[line_nr]) {
				DOLOG(debug, false, "DZ11 line %d state changed to %d", line_nr, is_connected);
#if defined(ESP32)
				Serial.printf("DZ11 line %d state changed to %d\r\n", line_nr, is_connected);
#endif

				connected[line_nr] = is_connected;

				// set CO & RI(NG)
				registers[(DZ11_MSR - DZ11_BASE) / 2] |= (1 << line_nr) | (1 << (line_nr + 8));
				// NO INTERRUPT
				// "The DZ11
				// data set control logic does not interrupt the POP-II processor when a carrier or ring signal changes
				// state. The program should periodically sample these registers to determine the current status. Sampling at a high rate is not necessary."
				// (3.3.8)
			}

			// receive data
			bool have_data = false;
			while(comm_interfaces.at(line_nr)->has_data()) {
				uint8_t buffer = comm_interfaces.at(line_nr)->get_byte();
				recv_buffers[line_nr].push_back(char(buffer));
				have_data = true;
			}

			if (have_data && is_rx_interrupt_enabled())
				trigger_interrupt(false);
		}
	}

	DOLOG(info, true, "DZ11 thread terminating");
}

void dz11::reset()
{
}

bool dz11::is_rx_interrupt_enabled() const
{
	return (registers[0] & 64) && (registers[0] & 32);
}

bool dz11::is_tx_interrupt_enabled() const
{
	return (registers[0] & 0x4000) && (registers[0] & 32);
}

uint8_t dz11::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr & ~1);
	if (addr & 1)
		return v >> 8;
	return v;
}

uint16_t dz11::read_word(const uint16_t addr)
{
	std::unique_lock<std::mutex> lck(input_lock);
	int      reg   = (addr - DZ11_BASE) / 2;
	uint16_t vtemp = registers[reg];

	if (addr == DZ11_CSR) {
		vtemp &= ~04033;
		if (flipflop_txd)
			vtemp |= 0x8700;  // set transmit ready bits
		flipflop_txd = !flipflop_txd;

		for(int i=0; i<dz11_n_lines; i++) {
			if (recv_buffers[i].empty() == false && (registers[(DZ11_TCR - DZ11_BASE) / 2] & (1 << i))) {
				vtemp |= 128;  // RDONE
				break;
			}
		}
	}
	else if (addr == DZ11_RBUF) {
		vtemp = 0;
		for(int i=0; i<dz11_n_lines; i++) {
			if (recv_buffers[i].empty() == false && (registers[(DZ11_TCR - DZ11_BASE) / 2] & (1 << i))) {
				uint8_t c = recv_buffers[i].front();
				recv_buffers[i].erase(recv_buffers[i].begin());
				vtemp = 0x8000 | (i << 8) | c | (parity(c) << 7);
				break;
			}
		}
	}
	else if (addr == DZ11_TCR) {
		vtemp = registers[reg];  // DTR, line enable
	}
	else if (addr == DZ11_MSR) {
		TRACE("msr start: %06o", registers[reg]);
		vtemp = registers[reg] & 0x00ff;  // keep ring indicator

		// add carrier detected bits
		for(size_t i=0; i<dz11_n_lines; i++) {
			if ((registers[(DZ11_TCR - DZ11_BASE) / 2] & (1 << i)) && (i < comm_interfaces.size() && connected[i]))
				vtemp |= 1 << (8 + i);
		}

		 // next read: no more RI
		registers[reg] &= 0xff00;
		TRACE("msr end: %06o, vtemp: %06o", registers[reg], vtemp);
	}

	TRACE("DZ11: read %06o from register %06o (\"%s\", %d)", vtemp, addr, dz11_register_names[reg], reg);

	return vtemp;
}

// FIXME locking
void dz11::write_byte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - DZ11_BASE) / 2];

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

void dz11::write_word(const uint16_t addr, const uint16_t v)
{
	int      reg   = (addr - DZ11_BASE) / 2;
	uint16_t v_set = v;
	TRACE("DZ11: write %06o to register %06o (\"%s\", %d)", v, addr, dz11_register_names[reg], reg);

	std::unique_lock<std::mutex> lck(input_lock);
	if (addr == DZ11_CSR) {
		if (v & 16) {  // CLR
			// reset
			trigger_interrupt(false);
			trigger_interrupt(true);
			v_set &= ~16;
		}
	}
	else if (addr == DZ11_TDR) {
		size_t line_nr = registers[reg] & 7;
		if (line_nr < comm_interfaces.size()) {
			char c = v & 127;  // mask off parity
			comm_interfaces.at(line_nr)->send_data(reinterpret_cast<const uint8_t *>(&c), 1);
			TRACE("DZ11 TRANSMIT %c (%d)", c, v);
		}

		if (is_tx_interrupt_enabled()) {
			TRACE("DZ11 INTERRUPT");
			trigger_interrupt(true);
		}
	}

	registers[reg] = v_set;
}

JsonDocument dz11::serialize() const
{
	JsonDocument j;

	JsonDocument j_interfaces;
	JsonArray    j_interfaces_work = j_interfaces.to<JsonArray>();
	for(auto & c: comm_interfaces)
		j_interfaces_work.add(c->serialize());
	j["interfaces"] = j_interfaces;

	for(int regnr=0; regnr<n_dz11_registers; regnr++)
		j[format("register-%d", regnr)] = registers[regnr];

	return j;
}

dz11 *dz11::deserialize(const JsonVariantConst j, bus *const b)
{
	std::vector<comm *> interfaces;

	JsonArrayConst j_interfaces = j["interfaces"];
	for(auto v: j_interfaces)
		interfaces.push_back(comm::deserialize(v, b));

	dz11 *r = new dz11(b, interfaces);
	r->begin();

	for(int regnr=0; regnr<n_dz11_registers; regnr++)
		r->registers[regnr] = j[format("register-%d", regnr)];

	return r;
}
