#include "gen.h"
#include <fcntl.h>
#include <unistd.h>
#if defined(BUILD_FOR_PICO2W)
#include <WiFiUdp.h>
#elif defined(_WIN32)
#include "win32.h"
#elif defined(ESP32)
#include <arpa/inet.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <sys/socket.h>
#elif defined(TEENSY4_1)
#else
#include <poll.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif

#include "eth_transport_vxlan.h"
#include "log.h"
#include "utils.h"

// https://www.rfc-editor.org/rfc/rfc7348

constexpr const int max_pkt_size = 1512;

eth_transport_vxlan::eth_transport_vxlan(const std::string & peer, const int port, const uint32_t id):
	peer(peer),
	port(port),
	id(id)
{
}

eth_transport_vxlan::~eth_transport_vxlan()
{
#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
	if (fd != -1)
		close(fd);
#endif
}

bool eth_transport_vxlan::begin()
{
#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) {
		DOLOG(log_ss::LS_ETH, "Cannot create socket: %s", strerror(errno));
		return false;
	}

	sockaddr_in listen_addr { };
	listen_addr.sin_family      = AF_INET;
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	listen_addr.sin_port        = htons(port);

	if (bind(fd, reinterpret_cast<struct sockaddr *>(&listen_addr), sizeof(listen_addr)) == -1) {
		DOLOG(log_ss::LS_ETH, "Cannot bind to port %d: %s", port, strerror(errno));
		close(fd);
		fd = -1;
		return false;
	}
#endif
	return true;
}

std::string eth_transport_vxlan::identifier() const
{
	return format("vxlan:%s:%d/%d", peer.c_str(), port, id);
}

bool eth_transport_vxlan::transmit(const uint8_t *const data, const size_t n_bytes)
{
	bool     rc        = true;
	size_t   wrapped_n = n_bytes + 8;
	uint8_t *wrapped   = new uint8_t[wrapped_n]();
	wrapped[0] = 0x08;
	wrapped[4] = id >> 16;
	wrapped[5] = id >> 8;
	wrapped[6] = id;
	memcpy(&wrapped[8], data, n_bytes);

#if defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
	udp.begin(port);
	udp.beginPacket(peer.c_str(), port);
	udp.write(data, n_bytes);
	if (udp.endPacket() == 0)
		rc = false;
#else
	sockaddr_in serveraddr { };
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port   = htons(port);

#if defined(_WIN32)
#ifdef _WIN32_WINNT
	serveraddr.sin_addr.s_addr = inet_addr(peer.c_str());
#else
	if (inet_pton(AF_INET, peer.c_str(), &serveraddr.sin_addr) == 0) {
		delete [] wrapped;
		DOLOG(log_ss::LS_ETH, "inet_pton(%s) failed", peer.c_str());
		return false;
	}
#endif
#else
	if (inet_aton(peer.c_str(), &serveraddr.sin_addr) == 0) {
		delete [] wrapped;
		DOLOG(log_ss::LS_ETH, "inet_aton(%s) failed", peer.c_str());
		return false;
	}
#endif

	if (sendto(fd, reinterpret_cast<const char *>(wrapped), wrapped_n, 0, reinterpret_cast<const sockaddr *>(&serveraddr), sizeof serveraddr) == -1) {
		DOLOG(log_ss::LS_ETH, "sendto failed: %s", strerror(errno));
		rc = false;
	}
#endif

	delete [] wrapped;
	pkt_cnt_tx++;

	return rc;
}

std::pair<uint8_t *, size_t> eth_transport_vxlan::get(const int timeout)
{
	uint8_t *pkt         = nullptr;
	size_t   packet_size = 0;
#if defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
	auto start = millis();
	while(millis() - start < timeout) {
		int rc = udp.parsePacket();
		if (rc > 0) {
			pkt = new uint8_t[rc];
			if (!pkt) {
				DOLOG(log_ss::LS_ETH, "malloc issue");
				return { nullptr, 0 };
			}
			udp.read(pkt, rc);
			packet_size = rc;
			break;
		}
	}

	if (!pkt)
		return { nullptr, 0 };
#else
#if defined(_WIN32)
	WSAPOLLFD fds[] { { fd, POLLIN, 0 } };
	int rc = WSAPoll(fds, 1, timeout);
#else
	pollfd    fds[] { { fd, POLLIN, 0 } };
	int rc = poll(fds, 1, timeout);
#endif
	if (rc <= 0)
		return { nullptr, 0 };

	pkt = new uint8_t[max_pkt_size]();
	int      rc2 = recv(fd, reinterpret_cast<char *>(pkt), max_pkt_size, 0);
	if (rc2 == -1) {
		delete [] pkt;
		return { nullptr, 0 };
	}
	packet_size = rc2;
#endif
	pkt_cnt_rx++;
	if (packet_size < 14 + 8) {
		delete [] pkt;
		return { nullptr, 0 };
	}

	uint32_t their_id = (pkt[4] << 16) | (pkt[5] << 8) | pkt[6];
	if (pkt[0] != 0x08 || their_id != id) {
		DOLOG(log_ss::LS_ETH, "vxlan id mismatch: %02x != %02x", their_id, id);
		delete [] pkt;
		return { nullptr, 0 };
	}

	packet_size -= 8;
	memmove(&pkt[0], &pkt[8], packet_size);
	return { pkt, packet_size };
}
