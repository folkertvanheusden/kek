// (C) 2024-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32) || defined(BUILD_FOR_RP2040)
#include <Arduino.h>
#endif
#if defined(ESP32) || defined(BUILD_FOR_RP2040)
#include <lwip/sockets.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <driver/uart.h>
#elif defined(_WIN32)
#include "win32.h"
#else
#define closesocket close
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


void comm_tcp_socket_server::setup_telnet_session()
{
	uint8_t dont_auth[]        = { 0xff, 0xf4, 0x25 };
	uint8_t suppress_goahead[] = { 0xff, 0xfb, 0x03 };
	uint8_t dont_linemode[]    = { 0xff, 0xfe, 0x22 };
	uint8_t dont_new_env[]     = { 0xff, 0xfe, 0x27 };
	uint8_t will_echo[]        = { 0xff, 0xfb, 0x01 };
	uint8_t dont_echo[]        = { 0xff, 0xfe, 0x01 };
	uint8_t noecho[]           = { 0xff, 0xfd, 0x2d };
	// uint8_t charset[]          = { 0xff, 0xfb, 0x01 };

	send_data(dont_auth, sizeof dont_auth);
	send_data(suppress_goahead, sizeof suppress_goahead);
	send_data(dont_linemode, sizeof dont_linemode);
	send_data(dont_new_env, sizeof dont_new_env);
	send_data(will_echo, sizeof will_echo);
	send_data(dont_echo, sizeof dont_echo);
	send_data(noecho, sizeof noecho);
}

comm_tcp_socket_server::comm_tcp_socket_server(const int port, const bool setup_telnet) :
	port(port),
	setup_telnet(setup_telnet)
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
		closesocket(fd);
	if (cfd != INVALID_SOCKET)
		closesocket(cfd);

	DOLOG(debug, false, "comm_tcp_socket_server: destructor for port %d finished", port);
}

bool comm_tcp_socket_server::begin()
{
	th = new std::thread(std::ref(*this));

	return true;
}

bool comm_tcp_socket_server::is_connected()
{
	my_unique_lock lck(&cfd_lock);

	return cfd != INVALID_SOCKET;
}

bool comm_tcp_socket_server::has_data()
{
	my_unique_lock lck(&cfd_lock);
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
		my_unique_lock lck(&cfd_lock);
		use_fd = cfd;
	}

	uint8_t c = 0;
#if defined(_WIN32)
	if (recv(use_fd, reinterpret_cast<char *>(&c), 1, 0) <= 0) {
		DOLOG(warning, false, "comm_tcp_socket_server::get_byte: failed");
		my_unique_lock lck(&cfd_lock);
		closesocket(cfd);
		cfd = INVALID_SOCKET;
	}
#else
	if (read(use_fd, reinterpret_cast<char *>(&c), 1) <= 0) {
		DOLOG(warning, false, "comm_tcp_socket_server::get_byte: failed");
		my_unique_lock lck(&cfd_lock);
		close(cfd);
		cfd = -1;
	}
#endif

	return c;
}

void comm_tcp_socket_server::send_data(const uint8_t *const in, const size_t n)
{
	const uint8_t *p   = in;
	size_t         len = n;

	while(len > 0) {
		my_unique_lock lck(&cfd_lock);
#if defined(_WIN32)
		int rc = send(cfd, reinterpret_cast<const char *>(p), len, 0);
		if (rc <= 0) {
			DOLOG(warning, false, "comm_tcp_socket_client::send_data: failed");
			closesocket(cfd);
			cfd = INVALID_SOCKET;
			break;
		}
#else
		int rc = write(cfd, p, len);
		if (rc <= 0) {
			DOLOG(warning, false, "comm_tcp_socket_client::send_data: failed");
			close(cfd);
			cfd = INVALID_SOCKET;
			break;
		}
#endif

		p   += rc;
		len -= rc;
	}
}

void comm_tcp_socket_server::operator()()
{
	set_thread_name("kek:COMMTCPS");

	DOLOG(info, true, "TCP comm thread started for port %d", port);

	fd = socket(AF_INET, SOCK_STREAM, 0);

#if !defined(_WIN32)
	int reuse_addr = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&reuse_addr), sizeof(reuse_addr)) == -1) {
		closesocket(fd);
		fd = INVALID_SOCKET;

		DOLOG(warning, true, "Cannot set reuseaddress for port %d (comm_tcp_socket_server)", port);
		return;
	}
#endif
	set_nodelay(fd);

	sockaddr_in listen_addr;
	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family      = AF_INET;
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	listen_addr.sin_port        = htons(port);

	if (bind(fd, reinterpret_cast<struct sockaddr *>(&listen_addr), sizeof(listen_addr)) == -1) {
		DOLOG(warning, true, "Cannot bind to port %d (send_datacomm_tcp_socket_server): %s", port, strerror(errno));

		closesocket(fd);
		fd = INVALID_SOCKET;
		return;
	}

	if (listen(fd, SOMAXCONN) == -1) {
		closesocket(fd);
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

		my_unique_lock lck(&cfd_lock);

		// disconnect any existing client session
		// yes, one can 'DOS' with this
		if (cfd != INVALID_SOCKET) {
			closesocket(cfd);
			DOLOG(info, false, "Restarting session for port %d", port);
		}

		cfd = accept(fd, nullptr, nullptr);

		if (cfd != INVALID_SOCKET) {
			set_nodelay(cfd);

			DOLOG(info, false, "Connected with %s", get_endpoint_name(cfd).c_str());
		}

		if (setup_telnet)
			setup_telnet_session();
	}

	DOLOG(info, true, "comm_tcp_socket_server thread terminating");
}

JsonDocument comm_tcp_socket_server::serialize() const
{
	JsonDocument j;

	j["comm-backend-type"] = "tcp-server";
	j["port"] = port;
	j["setup-telnet"] = setup_telnet;

	return j;
}

comm_tcp_socket_server *comm_tcp_socket_server::deserialize(const JsonVariantConst j)
{
	comm_tcp_socket_server *r = new comm_tcp_socket_server(j["port"].as<int>(), j["setup-telnet"].as<bool>());
	return r;
}
