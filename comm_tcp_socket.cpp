// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32)
#include <Arduino.h>
#endif
#if defined(ESP32)
#include <lwip/sockets.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <driver/uart.h>
#elif defined(_WIN32)
#include <ws2tcpip.h>
#include <winsock2.h>
#else
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#if IS_POSIX
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <thread>
#endif

#include "comm_tcp_socket.h"
#include "log.h"
#include "utils.h"


comm_tcp_socket::comm_tcp_socket(const int port)
{
	fd = socket(AF_INET, SOCK_STREAM, 0);

	int reuse_addr = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse_addr, sizeof(reuse_addr)) == -1) {
		close(fd);
		fd = INVALID_SOCKET;

		DOLOG(warning, true, "Cannot set reuseaddress for port %d (comm_tcp_socket)", port);
		return;
	}

	set_nodelay(fd);

	sockaddr_in listen_addr;
	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family      = AF_INET;
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	listen_addr.sin_port        = htons(port);

	if (bind(fd, reinterpret_cast<struct sockaddr *>(&listen_addr), sizeof(listen_addr)) == -1) {
		close(fd);
		fd = INVALID_SOCKET;

		DOLOG(warning, true, "Cannot bind to port %d (comm_tcp_socket)", port);
		return;
	}

	if (listen(fd, SOMAXCONN) == -1) {
		close(fd);
		fd = INVALID_SOCKET;

		DOLOG(warning, true, "Cannot listen on port %d (comm_tcp_socket)", port);
		return;
	}
}

comm_tcp_socket::~comm_tcp_socket()
{
}

bool comm_tcp_socket::is_connected()
{
}

bool comm_tcp_socket::has_data()
{
#if defined(_WIN32)
	WSAPOLLFD fds[] { { fd, POLLIN, 0 } };
	int rc = WSAPoll(fds, 1, 0);
#else
	pollfd fds[] { { fd, POLLIN, 0 } };
	int rc = poll(fds, 1, 0);
#endif

	return rc == 1;
}

uint8_t comm_tcp_socket::get_byte()
{
	uint8_t c = 0;
	read(fd, &c, 1);  // TODO error checking

	return c;
}

void comm_tcp_socket::send_data(const uint8_t *const in, const size_t n)
{
	const uint8_t *p   = in;
	size_t         len = n;

	while(len > 0) {
		int rc = write(fd, p, len);
		if (rc <= 0)  // TODO error checking
			break;

		p   += rc;
		len -= rc;
	}
}
