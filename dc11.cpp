// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include <cstring>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "bus.h"
#include "cpu.h"
#include "dc11.h"
#include "log.h"


dc11::dc11(const int base_port, bus *const b):
	base_port(base_port),
	b(b)
{
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

void dc11::operator()()
{
	int fds[dc11_n_lines] = { };

	pollfd pfds[8] = { };

	for(int i=0; i<dc11_n_lines; i++) {
		// listen on port
		pfds[i].fd = socket(AF_INET, SOCK_STREAM, 0);

		int port = base_port + i + 1;

	        sockaddr_in listen_addr;
		memset(&listen_addr, 0, sizeof(listen_addr));
		listen_addr.sin_family      = AF_INET;
		listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		listen_addr.sin_port        = htons(port);

		if (bind(pfds[i].fd, reinterpret_cast<struct sockaddr *>(&listen_addr), sizeof(listen_addr)) == -1) {
			close(pfds[i].fd);
			fds[i] = -1;

			DOLOG(warning, true, "Cannot bind to port %d (DC11)", port);
		}

		pfds[i].events = POLLIN;

		// client session
		pfds[dc11_n_lines + i].fd     = socket(AF_INET, SOCK_STREAM, 0);
		pfds[dc11_n_lines + i].events = POLLIN;
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
				DOLOG(info, false, "Failed reading on port %d", base_port + i + 1);
				close(pfds[i].fd);
				pfds[i].fd = -1;
			}
			else {
				int line_nr = i - dc11_n_lines;

				std::unique_lock<std::mutex> lck(input_lock[line_nr]);

				for(int k=0; k<rc; k++)
					recv_buffers[line_nr].push_back(buffer[k]);

				have_data[line_nr].notify_all();

				if (registers[line_nr * 4] & 64)  // interrupts enabled?
					b->getCpu()->queue_interrupt(4, 0320 + line_nr * 4);
			}
		}
	}

	for(int i=0; i<dc11_n_lines * 2; i++) {
		if (fds[i] != -1)
			close(fds[i]);
	}
}

void dc11::reset()
{
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
	const int reg   = (addr - DC11_BASE) / 2;

	uint16_t  vtemp = registers[reg];

	DOLOG(debug, false, "DC11: read register %06o (%d): %06o", addr, reg, vtemp);

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
	const int reg = (addr - DC11_BASE) / 2;

	DOLOG(debug, false, "DC11: write register %06o (%d) to %o", addr, reg, v);

	registers[reg] = v;
}
