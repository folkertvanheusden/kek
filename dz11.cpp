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

const char *const dz11_register_names[] { "R0_CSR", "R2_RBUF_LPR", "R4_TCR", "R6_MSR_TDR" };

dz11::dz11(bus *const b, const comm_io & io_channels):
	b(b),
	io_channels(io_channels)
{
	connected     .resize(dz11_n_lines);
	parity_setting.resize(dz11_n_lines);

	reset();
	registers[0] = 0x8000;
}

dz11::~dz11()
{
	DOLOG(debug, false, "DZ11 closing");

	stop_flag = true;

	if (th) {
		th->join();
		delete th;
	}
}

void dz11::show_state(console *const cnsl) const
{
	for(size_t i=0; i<dz11_n_lines; i++) {
		std::unique_lock<std::mutex> lck(input_lock);
		std::string out = format(" line %zu: %zu characters in buffer, ", i + 1, recv_buffers[i].size());
		if (connected[i] == NOT_CONNECTED)
			out += "not connected";
		else if (connected[i] == PENDING)
			out += "pending";
		else if (connected[i] == CONNECTED)
			out += "connected";
		else
			out += "?";
		cnsl->put_string_lf(out);
	}

	cnsl->put_string_lf(format(" RX interrupt enabled: %s", is_rx_interrupt_enabled() ? "true": "false" ));
	cnsl->put_string_lf(format(" TX interrupt enabled: %s", is_tx_interrupt_enabled() ? "true": "false" ));

	for(int i=0; i<4; i++)
		cnsl->put_string_lf(format(" register %d: %06o", i, registers[i]));
}

bool dz11::begin()
{
	th = new std::thread(std::ref(*this));

	return true;
}

void dz11::test_port(const size_t nr, const std::string & txt) const
{
	DOLOG(info, false, "DZ11 test line %zu", nr);

	io_channels.send_data(nr, reinterpret_cast<const uint8_t *>(txt.c_str()), txt.size());
}

void dz11::test_ports(const std::string & txt) const
{
	for(int i=0; i<dz11_n_lines; i++)
		test_port(i, txt);
}

void dz11::trigger_interrupt(const bool is_tx)
{
	TRACE("DZ11: %s interrupt", is_tx ? "TX" : "RX");
	b->getCpu()->queue_interrupt(DZ11_INTERRUPT_LEVEL, is_tx ? DZ11_INTERRUPT_VECTOR_TX : DZ11_INTERRUPT_VECTOR_RX);
}

#ifdef UNIT_TEST
void dz11::wait_connected(const int line_nr) const
{
	for(;;) {
		usleep(1000);

		std::unique_lock<std::mutex> lck(input_lock);
		if (connected[line_nr] != NOT_CONNECTED)
			break;
	}
}

void dz11::wait_have_data(const int line_nr) const
{
	for(;;) {
		usleep(1000);

		std::unique_lock<std::mutex> lck(input_lock);
		if (recv_buffers[line_nr].empty() == false)
			break;
	}
}
#endif

void dz11::operator()()
{
	set_thread_name("kek:DZ11");

	DOLOG(info, true, "DZ11 thread started");

	while(!stop_flag) {
		myusleep(10000);  // TODO replace polling

		for(int line_nr=0; line_nr<dz11_n_lines; line_nr++) {
			std::unique_lock<std::mutex> lck(input_lock);

			// (dis-)connected?
			bool is_connected  = io_channels.is_connected(line_nr);
			bool was_connected = connected[line_nr] != NOT_CONNECTED;

			if (is_connected != was_connected) {
				DOLOG(debug, false, "DZ11 line %d state changed to %d", line_nr, is_connected);
#if defined(ESP32)
				Serial.printf("DZ11 line %d state changed to %d\r\n", line_nr, is_connected);
#endif

				connected[line_nr] = is_connected ? PENDING : NOT_CONNECTED;

				// set CO & RI(NG)
				uint16_t mask1 = 1 << line_nr;
				uint16_t mask2 = 1 << (line_nr + 8);
				if (is_connected)
					registers[3] |= mask1 | mask2;
				else {
					registers[3] &= ~(mask1 | mask2);
					registers[2] &= ~mask2;
				}
				// NO INTERRUPT
				// "The DZ11
				// data set control logic does not interrupt the PDP-II processor when a carrier or ring signal changes
				// state. The program should periodically sample these registers to determine the current status. Sampling at a high rate is not necessary."
				// (3.3.8)
			}

			// receive data
			bool have_data = false;
			while(io_channels.has_data(line_nr)) {
				uint8_t buffer = io_channels.get_byte(line_nr);
				recv_buffers[line_nr].push_back(char(buffer));
				have_data = true;
			}

			if (have_data) {
				// registers[2]: LINE ENAB
				if (is_rx_interrupt_enabled()) {
					TRACE("DZ11: have data, trigger interrupt");
					trigger_interrupt(false);
				}
				else {
					TRACE("DZ11: have data, interrupt disabled! (%06o)", registers[0]);
				}
			}
		}
	}

	DOLOG(info, true, "DZ11 thread terminating");
}

void dz11::reset()
{
	for(int i=0; i<4; i++)
		registers[i] = 0;
}

bool dz11::is_rx_interrupt_enabled() const
{
	return (registers[0] & 0x0040) && (registers[0] & 32);
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
		if (registers[reg] & 0x10)  // CLR
			reset();  // vtemp is not affected so will be ...1. once when read

		vtemp &= ~128;
		for(int i=0; i<dz11_n_lines; i++) {
			if (recv_buffers[i].empty() == false) {
				TRACE("DZ11 CSR: line %d has data", i);
				vtemp |= 128;  // RDONE
				break;
			}
		}

		vtemp &= ~0x8000;  // TRDY
		if (registers[2] & 0xff)
			vtemp |= 0x8000;

		vtemp &= 0xf8ff;  // add current tx line
		vtemp |= scanner_line_nr << 8;

		vtemp &= ~7;  // mask off at least bit 0 off (used for DZ32 mode in BSD 2.11)
	}
	else if (addr == DZ11_RBUF) {
		vtemp = 0;
		for(int i=0; i<dz11_n_lines; i++) {
			if (recv_buffers[i].empty() == false) {
				uint8_t c = recv_buffers[i].front();
				recv_buffers[i].erase(recv_buffers[i].begin());
				bool    p = false;
				if (parity_setting[i] == EVEN_PARITY)
					p = !parity(c);
				else if (parity_setting[i] == ODD_PARITY)
					p = parity(c);
				vtemp = 0x8000 | (i << 8) | c | (p << 7);
				break;
			}
		}
	}
	else if (addr == DZ11_TCR) {
		/* as is */ // DTR, line enable
	}
	else if (addr == DZ11_MSR) {
		vtemp = registers[reg] & 0x00ff;  // keep ring indicator

		// add carrier detected bits (when DTR is set)
		for(int i=0; i<dz11_n_lines; i++) {
			if (i < dz11_n_lines && connected[i])
				vtemp |= 1 << (8 + i);
		}

		 // next read: no more RI
		registers[reg] &= 0xff00;
	}

	TRACE("DZ11: read %06o from register %06o (\"%s\", %d)", vtemp, addr, dz11_register_names[reg], reg);

	return vtemp;
}

// FIXME locking
void dz11::write_byte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - DZ11_BASE) / 2];
	if (addr & 1) {
		TRACE("DZ11 write byte %03o at odd address %06o", v, addr);
		vtemp &= 0x00ff;
		vtemp |= v << 8;
	}
	else {
		vtemp &= 0xff00;
		vtemp |= v;
	}
	write_word(addr & ~1, vtemp);
}

void dz11::tx_scanner_do(const int line, const bool force)
{
	registers[0] &= ~0x700;
	registers[0] |= line << 8;  // set transmit ready bits
	scanner_line_nr = line;

	registers[0] |= 0x8000;  // TRDY

	if (is_tx_interrupt_enabled() || force) {
		TRACE("DZ11 TX INTERRUPT for line %zu", line);
		trigger_interrupt(true);
	}
}

void dz11::tx_scanner(const std::optional<int> line, const bool force)
{
	if (line.has_value()) {
		int use_line_nr = line.value();
		TRACE("DZ11 specific line interrupt: %zu", use_line_nr);
		tx_scanner_do(use_line_nr, force);
	}
}

void dz11::write_word(const uint16_t addr, const uint16_t v)
{
	int      reg   = (addr - DZ11_BASE) / 2;
	uint16_t v_set = v;
	TRACE("DZ11: write %06o to register %06o (\"%s\", %d)", v, addr, dz11_register_names[reg], reg);

	std::unique_lock<std::mutex> lck(input_lock);
	if (addr == DZ11_CSR) {
		bool clr = v & 16;

		if (clr) {  // CLR
			reset();
			registers[0] &= ~16;
			trigger_interrupt(false);
		}

		// certain bits are read only
		v_set = (registers[0] & ~0x5078) | (v & 0x5078) | (clr ? 16 : 0);

		tx_scanner({ });
	}
	else if (addr == DZ11_LPR) {
		int line_nr = v & 7;
		if (line_nr < dz11_n_lines) {
			if (v & 64)  // parity enabled?
				parity_setting[line_nr] = v & 128 ? ODD_PARITY : EVEN_PARITY;
			else
				parity_setting[line_nr] = NO_PARITY;
		}
	}
	else if (addr == DZ11_TDR) {
		int line_nr = (registers[0] >> 8) & 7;
		if (line_nr < dz11_n_lines) {
			char c = parity_setting[line_nr] != NO_PARITY ? v & 127 : v;  // mask off parity
			io_channels.send_data(line_nr, reinterpret_cast<const uint8_t *>(&c), 1);
			TRACE("DZ11 TRANSMIT %c (%d) on line %d", c, v, line_nr);
		}

		tx_scanner(line_nr);
	}
	else if (addr == DZ11_TCR) {
		for(size_t i=0; i<dz11_n_lines; i++) {
			uint16_t mask = 1 << i;
			if (v & mask) {
				if (connected[i] == PENDING)
					connected[i] = CONNECTED;
				if (connected[i] == CONNECTED)
					tx_scanner(i, true);
			}
		}

		if ((v & 0xff) == 0) {
			TRACE("DZ11: unqueuing any pending interrupts");
			b->getCpu()->unqueue_interrupt(DZ11_INTERRUPT_LEVEL, DZ11_INTERRUPT_VECTOR_RX);
			b->getCpu()->unqueue_interrupt(DZ11_INTERRUPT_LEVEL, DZ11_INTERRUPT_VECTOR_TX);
		}
	}

	registers[reg] = v_set;
}

JsonDocument dz11::serialize() const
{
	JsonDocument j;
	j["interfaces"] = io_channels.serialize();

	for(int regnr=0; regnr<n_dz11_registers; regnr++)
		j[format("register-%d", regnr)] = registers[regnr];

	return j;
}

dz11 * dz11::deserialize(const JsonVariantConst j, bus *const b)
{
	comm_io io_channels = comm_io::deserialize(j["interfaces"], b);

	dz11 *r = new dz11(b, io_channels);
	r->begin();

	for(int regnr=0; regnr<n_dz11_registers; regnr++)
		r->registers[regnr] = j[format("register-%d", regnr)];

	return r;
}

#if defined(UNIT_TEST)
#include <stdexcept>
#include <gtest/gtest.h>

#include "comm_unittest_helper.h"

bool has_irq(cpu *const c, const int level, const uint16_t vector)
{
	auto irqs_for_level = c->get_queued_interrupts().find(level);
	return irqs_for_level->second.find(vector) != irqs_for_level->second.end();
}

TEST(dz11, dz11tests) {
	settrace(true);
	setlogfile("dz11-test.log", debug, debug, true);

	bus  b;
	std::atomic_uint32_t event { 0 };
	cpu *c = new cpu(&b, &event);
	b.add_cpu(c);
	c->setPSW_spl(7);  // allow IRQs from DZ11 (level 5)

	comm_io io;
	comm_unittest_helper *tty1 = new comm_unittest_helper();
	comm_unittest_helper *tty2 = new comm_unittest_helper();
	io.set_device(0, tty1);
	io.set_device(1, tty2);
	dz11 d(&b, &io);

	// object init test
	EXPECT_EQ(d.begin(), true);

	// at start, TRDY must be set for BSD2.11 to acknowledge the port
	EXPECT_EQ(d.read_word(0160100), 0x8000);

	// see if setting the protected bits change them
	d.write_word(0160100, 027607);
	EXPECT_EQ(d.read_word(0160100) & 0x7fff, 0);  // should not change them (ignore TRDY)

	// reset device
	d.write_word(0160100, 020);  // 'CLR' (= reset)
	EXPECT_EQ(d.read_word(0160100) & 020, 020);
	EXPECT_EQ(d.read_word(0160100) & 020, 0);
	// sources say: no interrupt on CLR (RX/TX) yet BSD 2.11 expects an RX irq
	EXPECT_EQ(has_irq(c, 5, 0310), true);  // RX
	EXPECT_EQ(has_irq(c, 5, 0314), false);  // TX
	c->init_interrupt_queue();  // clear pending interrupt

	// check CO in MSR for a new connection
	tty1->set_connected(true);
	d.wait_connected(0);  // wait for thread to notice the "incoming connection"
	EXPECT_EQ(d.read_word(0160106), 0x101);  // RING
	EXPECT_EQ(d.read_word(0160106), 0x100);  // CO (carrier detected)
	tty2->set_connected(true);
	d.wait_connected(1);  // wait for thread to notice the "incoming connection"
	EXPECT_EQ(d.read_word(0160106), 0x302);  // RING
	EXPECT_EQ(d.read_word(0160106), 0x300);  // CO (carrier detected)
	EXPECT_EQ(has_irq(c, 5, 0310), false);  // RX
	EXPECT_EQ(has_irq(c, 5, 0314), false);  // TX

	d.write_word(0160100, 0x4060);  // TIE/RIE/MSE

	// data RX
	d.write_word(0160104, 3);  // enable line 0 & 1
	tty1->set_data({ 0x99 });
	d.wait_have_data(0);
	EXPECT_EQ(d.read_word(0160100) & 128, 128);  // RDONE
	uint16_t rbuf1 = d.read_word(0160102);
	EXPECT_EQ((rbuf1 >> 8) & 7, 0);  // RX LINE
	EXPECT_EQ(rbuf1 & 0x8000, 0x8000);  // DATA VALID
	EXPECT_EQ(rbuf1 & 255, 0x99);  // RBUF
	EXPECT_EQ(has_irq(c, 5, 0310), true);  // RX
	// is now empty
	rbuf1 = d.read_word(0160100);
	EXPECT_EQ(rbuf1 & 0x80, 0);  // DATA VALID / RDONE
	EXPECT_EQ(d.read_word(0160102) & 0x80ff, 0);  // DATA VALID, RBUF
	EXPECT_EQ(has_irq(c, 5, 0310), true);  // RX (not cleared)
	// test line2
	tty2->set_data({ 0xaa });
	d.wait_have_data(1);
	EXPECT_EQ(d.read_word(0160100) & 128, 128);  // RDONE
	uint16_t rbuf2 = d.read_word(0160102);
	EXPECT_EQ((rbuf2 >> 8) & 7, 1);  // RX LINE
	EXPECT_EQ(rbuf2 & 0x8000, 0x8000);  // DATA VALID
	EXPECT_EQ(rbuf2 & 255, 0xaa);  // RBUF
	EXPECT_EQ(has_irq(c, 5, 0310), true);  // RX
	// is now empty
	EXPECT_EQ(d.read_word(0160100) & 128, 0);  // RDONE
	EXPECT_EQ(d.read_word(0160102) & 0x80ff, 0);  // DATA VALID / RBUF
	EXPECT_EQ(has_irq(c, 5, 0310), true);  // RX (not cleared)
	c->init_interrupt_queue();  // clear pending interrupts
	// data on a disabled line
	d.write_word(0160104, 1);  // enable line 0 (1 disabled)
	tty2->set_data({ 123 });
	d.wait_have_data(1);
	EXPECT_EQ(d.read_word(0160100) & 128, 0);  // RDONE
	EXPECT_EQ(d.read_word(0160102) & 0x87ff, 0);  // DATA VALID, RX LINE, RBUF
	EXPECT_EQ(has_irq(c, 5, 0310), false);  // RX

	// data TX
	d.write_word(0160104, 3);  // enable line 0 & 1
	int line = (d.read_word(0160100) >> 8) & 7;
	d.write_word(0160106, 0x44);  // TBUF
	std::vector<uint8_t> data_tx[] { tty1->get_tx_data(), tty2->get_tx_data() };
	EXPECT_EQ(data_tx[line].size(), 1);  // has data
	EXPECT_EQ(data_tx[1 - line].size(), 0);  // has no data
	EXPECT_EQ(has_irq(c, 5, 0314), true);  // TX
	b.getCpu()->unqueue_interrupt(DZ11_INTERRUPT_LEVEL, DZ11_INTERRUPT_VECTOR_TX);
	EXPECT_EQ(has_irq(c, 5, 0314), false);  // TX
}
#endif
