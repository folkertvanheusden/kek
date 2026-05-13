#include "gen.h"
#include <fcntl.h>
#include <unistd.h>
#if defined(BUILD_FOR_RP2040)
#include <WiFiUdp.h>
#else
#include <poll.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif

#include "eth_transport_vxlan.h"
#include "log.h"
#include "utils.h"


constexpr const int max_pkt_size = 1512;

eth_transport_vxlan::eth_transport_vxlan(const std::string & peer, const int port):
	peer(peer),
	port(port)
{
}

eth_transport_vxlan::~eth_transport_vxlan()
{
	if (fd != -1)
		close(fd);
}

bool eth_transport_vxlan::begin()
{
#if !defined(BUILD_FOR_RP2040)
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) {
		DOLOG(debug, false, "Cannot create socket: %s", strerror(errno));
		return false;
	}
#endif
	return true;
}

void eth_transport_vxlan::transmit(const uint8_t *const data, const size_t n_bytes)
{
	size_t   wrapped_n = n_bytes + 8;
	uint8_t *wrapped   = new uint8_t[wrapped_n]();
	memcpy(&wrapped[8], data, n_bytes);

#if defined(BUILD_FOR_RP2040)
	udp.begin(port);
	udp.beginPacket(peer.c_str(), port);
	udp.write(data, n_bytes);
	udp.endPacket();
#else
	sockaddr_in serveraddr { };
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port   = htons(port);
	if (inet_aton(peer.c_str(), &serveraddr.sin_addr) == 0)
		DOLOG(debug, false, "inet_aton(%s) failed", peer.c_str());
	else if (sendto(fd, wrapped, wrapped_n, 0, reinterpret_cast<const sockaddr *>(&serveraddr), sizeof serveraddr) == -1)
		DOLOG(debug, false, "sendto failed: %s", strerror(errno));
#endif

	delete [] wrapped;
}

std::pair<uint8_t *, size_t> eth_transport_vxlan::get(const int timeout)
{
	uint8_t *reply       = nullptr;
	size_t   packet_size = 0;
#if defined(BUILD_FOR_RP2040)
	auto start = millis();
	while(millis() - start < timeout) {
		int rc = udp.parsePacket();
		if (rc > 0) {
			reply = new uint8_t[rc];
			if (!reply) {
				DOLOG(ll_critical, true, "malloc issue");
				return { nullptr, 0 };
			}
			udp.read(reply, rc);
			packet_size = rc;
			break;
		}
	}

	if (!reply)
		return { nullptr, 0 };
#else
	pollfd fds[] { { fd, POLLIN, 0 } };
	int    rc = poll(fds, 1, timeout);
	if (rc <= 0)
		return { nullptr, 0 };

	uint8_t *pkt = new uint8_t[max_pkt_size]();
	int      rc2 = recv(fd, pkt, max_pkt_size, 0);
	if (rc2 == -1) {
		delete [] pkt;
		return { nullptr, 0 };
	}
	packet_size = rc2;
#endif
	if (packet_size < 14 + 8) {
		delete [] pkt;
		return { nullptr, 0 };
	}

	packet_size -= 8;
	memmove(&pkt[0], &pkt[8], packet_size);
	return { pkt, packet_size };
}
