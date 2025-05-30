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
#include <cstring>
#if IS_POSIX
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#endif

#include "comm_tcp_socket_server.h"
#include "log.h"
#include "utils.h"


static bool setup_telnet_session(const int fd)
{
	uint8_t dont_auth[]        = { 0xff, 0xf4, 0x25 };
	uint8_t suppress_goahead[] = { 0xff, 0xfb, 0x03 };
	uint8_t dont_linemode[]    = { 0xff, 0xfe, 0x22 };
	uint8_t dont_new_env[]     = { 0xff, 0xfe, 0x27 };
	uint8_t will_echo[]        = { 0xff, 0xfb, 0x01 };
	uint8_t dont_echo[]        = { 0xff, 0xfe, 0x01 };
	uint8_t noecho[]           = { 0xff, 0xfd, 0x2d };
	// uint8_t charset[]          = { 0xff, 0xfb, 0x01 };

	if (write(fd, dont_auth, sizeof dont_auth) != sizeof dont_auth)
		return false;

	if (write(fd, suppress_goahead, sizeof suppress_goahead) != sizeof suppress_goahead)
		return false;

	if (write(fd, dont_linemode, sizeof dont_linemode) != sizeof dont_linemode)
		return false;

	if (write(fd, dont_new_env, sizeof dont_new_env) != sizeof dont_new_env)
		return false;

	if (write(fd, will_echo, sizeof will_echo) != sizeof will_echo)
		return false;

	if (write(fd, dont_echo, sizeof dont_echo) != sizeof dont_echo)
		return false;

	if (write(fd, noecho, sizeof noecho) != sizeof noecho)
		return false;

	return true;
}

comm_tcp_socket_server::comm_tcp_socket_server(const int port) : port(port)
{
}

comm_tcp_socket_server::~comm_tcp_socket_server()
{
	stop_flag = true;

	if (th) {
		th->join();
		delete th;
	}

	if (fd != INVALID_SOCKET)
		close(fd);
	if (cfd != INVALID_SOCKET)
		close(cfd);

	DOLOG(debug, false, "comm_tcp_socket_server: destructor for port %d finished", port);
}

bool comm_tcp_socket_server::begin()
{
	th = new std::thread(std::ref(*this));

	return true;
}

bool comm_tcp_socket_server::is_connected()
{
	std::unique_lock<std::mutex> lck(cfd_lock);

	return cfd != INVALID_SOCKET;
}

bool comm_tcp_socket_server::has_data()
{
	std::unique_lock<std::mutex> lck(cfd_lock);
#if defined(_WIN32)
	WSAPOLLFD fds[] { { cfd, POLLIN, 0 } };
	int rc = WSAPoll(fds, 1, 0);
#else
	pollfd    fds[] { { cfd, POLLIN, 0 } };
	int rc = poll(fds, 1, 0);
#endif

	return rc == 1;
}

uint8_t comm_tcp_socket_server::get_byte()
{
	int use_fd = -1;

	{
		std::unique_lock<std::mutex> lck(cfd_lock);
		use_fd = cfd;
	}

	uint8_t c = 0;
	if (read(use_fd, &c, 1) <= 0) {
		DOLOG(warning, false, " comm_tcp_socket_server::get_byte failed");
		std::unique_lock<std::mutex> lck(cfd_lock);
		close(cfd);
		cfd = INVALID_SOCKET;
	}

	return c;
}

void comm_tcp_socket_server::send_data(const uint8_t *const in, const size_t n)
{
	const uint8_t *p   = in;
	size_t         len = n;

	while(len > 0) {
		std::unique_lock<std::mutex> lck(cfd_lock);
		int rc = write(cfd, p, len);
		if (rc <= 0) {  // TODO error checking
			DOLOG(warning, false, " comm_tcp_socket_server::send_data failed");
			close(cfd);
			cfd = INVALID_SOCKET;
			break;
		}

		p   += rc;
		len -= rc;
	}
}

void comm_tcp_socket_server::operator()()
{
	set_thread_name("kek:COMMTCPS");

	DOLOG(info, true, "TCP comm thread started for port %d", port);

	fd = socket(AF_INET, SOCK_STREAM, 0);

	int reuse_addr = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&reuse_addr), sizeof(reuse_addr)) == -1) {
		close(fd);
		fd = INVALID_SOCKET;

		DOLOG(warning, true, "Cannot set reuseaddress for port %d (comm_tcp_socket_server)", port);
		return;
	}

	set_nodelay(fd);

	sockaddr_in listen_addr;
	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family      = AF_INET;
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	listen_addr.sin_port        = htons(port);

	if (bind(fd, reinterpret_cast<struct sockaddr *>(&listen_addr), sizeof(listen_addr)) == -1) {
		DOLOG(warning, true, "Cannot bind to port %d (send_datacomm_tcp_socket_server): %s", port, strerror(errno));

		close(fd);
		fd = INVALID_SOCKET;
		return;
	}

	if (listen(fd, SOMAXCONN) == -1) {
		close(fd);
		fd = INVALID_SOCKET;

		DOLOG(warning, true, "Cannot listen on port %d (comm_tcp_socket_server)", port);
		return;
	}

#if defined(_WIN32)
	WSAPOLLFD fds[] { { fd, POLLIN, 0 } };
#else
	pollfd    fds[] { { fd, POLLIN, 0 } };
#endif

	while(!stop_flag) {
#if defined(_WIN32)
		int rc = WSAPoll(fds, 1, 100);
#else
		int rc = poll(fds, 1, 100);
#endif
		if (rc == 0)
			continue;

		std::unique_lock<std::mutex> lck(cfd_lock);

		// disconnect any existing client session
		// yes, one can 'DOS' with this
		if (cfd != INVALID_SOCKET) {
			close(cfd);
			DOLOG(info, false, "Restarting session for port %d", port);
		}

		cfd = accept(fd, nullptr, nullptr);

		if (cfd != INVALID_SOCKET) {
			set_nodelay(cfd);

			DOLOG(info, false, "Connected with %s", get_endpoint_name(cfd).c_str());
		}

#if 0
		if (setup_telnet_session(cfd) == false) {
			close(cfd);
			cfd = INVALID_SOCKET;
		}
#endif
	}

	DOLOG(info, true, "comm_tcp_socket_server thread terminating");
}

JsonDocument comm_tcp_socket_server::serialize() const
{
	JsonDocument j;

	j["comm-backend-type"] = "tcp-server";

	j["port"] = port;

	return j;
}

comm_tcp_socket_server *comm_tcp_socket_server::deserialize(const JsonVariantConst j)
{
	comm_tcp_socket_server *r = new comm_tcp_socket_server(j["port"].as<int>());

	return r;
}
