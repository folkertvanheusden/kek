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

FLASHMEM bool ddp::set_target(const std::string & server, const int n_pixels)
{
	my_unique_lock lck(&lock);
	this->server   = server;
	this->n_pixels = n_pixels;
	return true;
}

FLASHMEM void ddp::push(console *cnsl, bus *const b, const uint8_t brightness)
{
	std::string ip;
	{
		my_unique_lock lck(&lock);
		ip = server;
	}

	uint8_t *message = nullptr;

	try {
		std::vector<std::tuple<uint8_t, uint8_t, uint8_t> > pixels;
		cnsl->generate_panel_colors(pixels, n_pixels, b, b->getCpu(), brightness);

		size_t   pixels_allocated = pixels.size();
		size_t   msg_len = 10 + pixels_allocated * 3;
		message = new uint8_t[msg_len]();
		message[0] = (1 << 6) |  // version
				1;  // push
		message[2] = (1 << 3) |  // RGB
			3;  // 8 bits per pixel element
		message[3] = 1;  // default output device
		message[8] = (pixels_allocated * 3) >> 8;  // data length
		message[9] = (pixels_allocated * 3) & 255;
		size_t offset = 10;
		for(auto pixel: pixels) {
			message[offset++] = std::get<0>(pixel);
			message[offset++] = std::get<1>(pixel);
			message[offset++] = std::get<2>(pixel);
		}

		send_message(ip, 4048, message, msg_len);
	}
	catch(int trap_nr) {
		DOLOG(log_ss::LS_GENERIC, "Trap %d caught in ddp::push", trap_nr);
	}
	catch(...) {
		// most likely a find() that failed
		DOLOG(log_ss::LS_GENERIC, "Unexpected exception in ddp::push (setup)");
	}

	delete [] message;
}

FLASHMEM void ddp::test()
{
	std::string ip;
	{
		my_unique_lock lck(&lock);
		ip = server;
	}

	uint8_t *message = nullptr;

	try {
		size_t msg_len = 10 + n_pixels * 3;
		message = new uint8_t[msg_len]();
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
	}
	catch(...) {
		// most likely a find() that failed
		DOLOG(log_ss::LS_GENERIC, "Unexpected exception in ddp::test");
	}

	delete [] message;
}
