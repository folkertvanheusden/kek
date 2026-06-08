// written by Folkert van Heusden <folkert@komputilo.nl>
// license under MIT license
#include "gen.h"
#if defined(BUILD_FOR_PICO2W)
#include <WiFi.h>
#elif defined(BUILD_FOR_PICO2W)
#include <WiFiUdp.h>
#elif defined(TEENSY4_1)
#include <QNEthernet.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#if defined(__FreeBSD__)
#include <netinet/in.h>
#endif
#endif

#include "ddp.h"
#include "bus.h"
#include "log.h"

#if defined(BUILD_FOR_PICO2W)
static WiFiUDP         *udp = new WiFiUDP;
#elif defined(TEENSY4_1)
static qn::EthernetUDP *udp = new qn::EthernetUDP;
#endif

FLASHMEM void send_message(const std::string & ip, const int port, const uint8_t *const data, const size_t n_bytes)
{
#if defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
	udp->begin(4048);
	udp->beginPacket(ip.c_str(), port);
	udp->write(data, n_bytes);
	udp->endPacket();
#else
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) {
		DOLOG(log_ss::LS_GENERIC, "Cannot create socket: %s", strerror(errno));
		return;
	}

	sockaddr_in serveraddr { };
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port   = htons(port);
#if defined(_WIN32)
#ifdef _WIN32_WINNT
	serveraddr.sin_addr.s_addr = inet_addr(ip.c_str());
#else
	if (inet_pton(AF_INET, ip.c_str(), &serveraddr.sin_addr) == 0) {
		DOLOG(log_ss::GENERIC, "inet_pton(%s) failed", ip.c_str());
		close(fd);
		return;
	}
#endif
#else
	if (inet_aton(ip.c_str(), &serveraddr.sin_addr) == 0) {
		DOLOG(log_ss::LS_GENERIC, "inet_aton(%s) failed", ip.c_str());
		close(fd);
		return;
	}
#endif

	if (sendto(fd, reinterpret_cast<const char *>(data), n_bytes, 0, reinterpret_cast<const sockaddr *>(&serveraddr), sizeof serveraddr) == -1) {
		DOLOG(log_ss::LS_GENERIC, "sendto failed: %s", strerror(errno));
	}

	close(fd);
#endif
}

FLASHMEM ddp::ddp()
{
}

FLASHMEM ddp::~ddp()
{
}

FLASHMEM bool ddp::begin()
{
	return true;
}

FLASHMEM bool ddp::set_target(const std::string & ip, const int n_pixels)
{
	my_unique_lock lck(&lock);
	server         = ip;
	this->n_pixels = n_pixels;
	return true;
}

FLASHMEM void ddp::push(bus *const b, const bool running_flag)
{
	std::string ip;
	{
		my_unique_lock lck(&lock);
		ip = server;
	}

	try {
		cpu     *const c    = b->getCpu();
		int      run_mode   = c->getPSW_runmode();
		uint16_t current_PC = c->getPC();

		size_t   msg_len = 10 + n_pixels * 3;
		uint8_t *message = new uint8_t[msg_len]();
		message[0] = (1 << 6) |  // version
				1;  // push
		message[2] = (1 << 3) |  // RGB
			3;  // 8 bits per pixel element
		message[3] = 1;  // default output device
		message[8] = (n_pixels * 3) >> 8;  // data length
		message[9] = (n_pixels * 3) & 255;

		int o = 10 + (current_PC * n_pixels / 65536) * 3;

		if (run_mode == 0)
			message[o + 0] = 255;  // red
		else if (run_mode == 1)
			message[o + 2] = 255;  // blue
		else if (run_mode == 2) {  // theoretically
			message[o + 0] = 255;
			message[o + 1] = 255;
		}
		else if (run_mode == 3) {  // green
			message[o + 1] = 255;
		}

		send_message(ip, 4048, message, msg_len);

		delete [] message;
	}
	catch(int trap_nr) {
		DOLOG(log_ss::LS_GENERIC, "Trap %d caught in ddp::push", trap_nr);
	}
	catch(...) {
		// most likely a find() that failed
		DOLOG(log_ss::LS_GENERIC, "Unexpected exception in ddp::push (setup)");
	}
}

FLASHMEM void ddp::test()
{
	std::string ip;
	{
		my_unique_lock lck(&lock);
		ip = server;
	}

	try {
		size_t   msg_len = 10 + n_pixels * 3;
		uint8_t *message = new uint8_t[msg_len]();
		message[0] = (1 << 6) |  // version
				1;  // push
		message[2] = (1 << 3) |  // RGB
			3;  // 8 bits per pixel element
		message[3] = 1;  // default output device
		message[8] = (n_pixels * 3) >> 8;  // data length
		message[9] = (n_pixels * 3) & 255;

		for(int i=0; i<n_pixels; i++) {
			message[10 + i * 3 + 1] = 255;
			send_message(ip, 4048, message, sizeof message);
			myusleep(10000);
		}

		memset(&message[10], 0, n_pixels * 3);
		send_message(ip, 4048, message, sizeof message);

		delete [] message;
	}
	catch(...) {
		// most likely a find() that failed
		DOLOG(log_ss::LS_GENERIC, "Unexpected exception in ddp::test");
	}
}
