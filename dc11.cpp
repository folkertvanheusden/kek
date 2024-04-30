// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "bus.h"
#include "cpu.h"
#include "dc11.h"
#include "log.h"
#include "utils.h"


dc11::dc11(const int base_port, bus *const b):
	base_port(base_port),
	b(b)
{
	// TODO move to begin()
	th = new std::thread(std::ref(*this));
}

dc11::~dc11()
{
	stop_flag = true;

	if (th) {
		th->join();
		delete th;
	}
}

void dc11::trigger_interrupt(const int line_nr)
{
	printf("Interrupt %d\r\n", line_nr);
	b->getCpu()->queue_interrupt(5, 0300 + line_nr * 4);
}

void dc11::operator()()
{
	set_thread_name("kek:DC11");

	DOLOG(info, true, "DC11 thread started");

	for(int i=0; i<dc11_n_lines; i++) {
		// client session
		pfds[dc11_n_lines + i].fd     = -1;
		pfds[dc11_n_lines + i].events = POLLIN;

		// listen on port
		int port = base_port + i + 1;

		pfds[i].fd = socket(AF_INET, SOCK_STREAM, 0);

		int reuse_addr = 1;
		if (setsockopt(pfds[i].fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse_addr, sizeof(reuse_addr)) == -1) {
			close(pfds[i].fd);
			pfds[i].fd = -1;

			DOLOG(warning, true, "Cannot set reuseaddress for port %d (DC11)", port);
			continue;
		}

	        sockaddr_in listen_addr;
		memset(&listen_addr, 0, sizeof(listen_addr));
		listen_addr.sin_family      = AF_INET;
		listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		listen_addr.sin_port        = htons(port);

		if (bind(pfds[i].fd, reinterpret_cast<struct sockaddr *>(&listen_addr), sizeof(listen_addr)) == -1) {
			close(pfds[i].fd);
			pfds[i].fd = -1;

			DOLOG(warning, true, "Cannot bind to port %d (DC11)", port);
			continue;
		}

		if (listen(pfds[i].fd, SOMAXCONN) == -1) {
			close(pfds[i].fd);
			pfds[i].fd = -1;

			DOLOG(warning, true, "Cannot listen on port %d (DC11)", port);
			continue;
		}

		pfds[i].events = POLLIN;
	}

	while(!stop_flag) {
		int rc = poll(pfds, dc11_n_lines * 2, 100);
		if (rc == 0)
			continue;

		// accept any new session
		for(int i=0; i<dc11_n_lines; i++) {
			if (pfds[i].revents != POLLIN)
				continue;

			int client_i = dc11_n_lines + i;

			// disconnect any existing client session
			// yes, one can ddos with this
			if (pfds[client_i].fd != -1) {
				close(pfds[client_i].fd);
				DOLOG(info, false, "Restarting session for port %d", base_port + i + 1);
			}

			pfds[client_i].fd = accept(pfds[i].fd, nullptr, nullptr);
		}

		// receive data
		for(int i=dc11_n_lines; i<dc11_n_lines * 2; i++) {
			if (pfds[i].revents != POLLIN)
				continue;

			char buffer[32] { };
			int rc = read(pfds[i].fd, buffer, sizeof buffer);
			if (rc <= 0) {  // closed or error?
				DOLOG(info, false, "Failed reading from port %d", i - dc11_n_lines + 1);
				close(pfds[i].fd);
				pfds[i].fd = -1;
			}
			else {
				int line_nr = i - dc11_n_lines;

				std::unique_lock<std::mutex> lck(input_lock[line_nr]);

				for(int k=0; k<rc; k++)
					recv_buffers[line_nr].push_back(buffer[k]);

				registers[line_nr * 4 + 0] |= 128;  // DONE: bit 7

				if (is_rx_interrupt_enabled(line_nr))
					trigger_interrupt(line_nr);

				have_data[line_nr].notify_all();
			}
		}
	}

	DOLOG(info, true, "DC11 thread terminating");

	for(int i=0; i<dc11_n_lines * 2; i++) {
		if (pfds[i].fd != -1)
			close(pfds[i].fd);
	}
}

void dc11::reset()
{
}

bool dc11::is_rx_interrupt_enabled(const int line_nr)
{
	return !!(registers[line_nr * 4 + 0] & 64);
}

bool dc11::is_tx_interrupt_enabled(const int line_nr)
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

	// emulate DTR, CTS & READY
	registers[line_nr * 4 + 0] &= ~1;  // DTR: bit 0  [RCSR]
	registers[line_nr * 4 + 0] &= ~4;  // CD : bit 2
	registers[line_nr * 4 + 2] &= ~2;  // CTS: bit 1  [TSCR]
	registers[line_nr * 4 + 2] &= ~128;  // READY: bit 7

	if (pfds[line_nr + dc11_n_lines].fd != -1) {
		registers[line_nr * 4 + 0] |= 1;
		registers[line_nr * 4 + 0] |= 4;
		registers[line_nr * 4 + 2] |= 2;
		registers[line_nr * 4 + 2] |= 128;
	}

	uint16_t vtemp   = registers[reg];

	if ((reg & 3) == 1) {  // read data register
		std::unique_lock<std::mutex> lck(input_lock[line_nr]);

		// get oldest byte in buffer
		if (recv_buffers[line_nr].empty() == false) {
			vtemp = *recv_buffers[line_nr].begin();
			printf("return: %d\n", vtemp);

			recv_buffers[line_nr].erase(recv_buffers[line_nr].begin());

			// still data in buffer? generate interrupt
			if (recv_buffers[line_nr].empty() == false) {
				registers[line_nr * 4 + 0] |= 128;  // DONE: bit 7

				if (is_rx_interrupt_enabled(line_nr))
					trigger_interrupt(line_nr);
			}
		}
	}

	DOLOG(debug, true, "DC11: read register %06o (line %d): %06o", addr, line_nr, vtemp);
	printf("DC11: read register %06o (line %d): %06o\r\n", addr, line_nr, vtemp);

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

void dc11::write_word(const uint16_t addr, uint16_t v)
{
	int reg     = (addr - DC11_BASE) / 2;
	int line_nr = reg / 4;

	DOLOG(debug, true, "DC11: write register %06o (%d line_nr %d) to %06o", addr, reg, line_nr, v);
	printf("DC11: write register %06o (%d line_nr %d) to %06o\r\n", addr, reg, line_nr, v);

	if ((reg & 3) == 3) {  // transmit buffer
		char c = v & 127;

		// TODO handle failed transmit
		printf("%ld\r\n", write(pfds[dc11_n_lines + line_nr].fd, &c, 1));

		if (is_tx_interrupt_enabled(line_nr))
			trigger_interrupt(line_nr);
	}

	registers[reg] = v;
}
