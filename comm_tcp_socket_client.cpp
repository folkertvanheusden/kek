// (C) 2024-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32) || defined(BUILD_FOR_PICO2W)
#include <Arduino.h>
#endif
#if defined(ESP32) || defined(BUILD_FOR_PICO2W)
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <driver/uart.h>
#elif defined(_WIN32)
#include "win32.h"
#else
#define closesocket close
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <cstring>
#if IS_POSIX
#include <errno.h>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#endif

#include "comm_tcp_socket_client.h"
#include "log.h"
#include "utils.h"


comm_tcp_socket_client::comm_tcp_socket_client(const std::string & host, const int port) :
	host(host),
	port(port)
{
}

comm_tcp_socket_client::~comm_tcp_socket_client()
{
	stop_flag = true;

	if (th) {
		th->join();
		delete th;
	}
}

bool comm_tcp_socket_client::begin()
{
	th = new std::thread(std::ref(*this));

	return true;
}

bool comm_tcp_socket_client::is_connected()
{
	my_unique_lock lck(&cfd_lock);

	return cfd != INVALID_SOCKET;
}

bool comm_tcp_socket_client::has_data()
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

uint8_t comm_tcp_socket_client::get_byte()
{
	int use_fd = -1;

	{
		my_unique_lock lck(&cfd_lock);
		use_fd = cfd;
	}

	uint8_t c = 0;
#if defined(_WIN32)
	if (recv(use_fd, reinterpret_cast<char *>(&c), 1, 0) <= 0) {
		DOLOG(log_ss::LS_COMM, "comm_tcp_socket_client::get_byte: failed");
		my_unique_lock lck(&cfd_lock);
		closesocket(cfd);
		cfd = INVALID_SOCKET;
	}
#else
	if (read(use_fd, reinterpret_cast<char *>(&c), 1) <= 0) {
		DOLOG(log_ss::LS_COMM, "comm_tcp_socket_client::get_byte: failed");
		my_unique_lock lck(&cfd_lock);
		close(cfd);
		cfd = -1;
	}
#endif

	return c;
}

void comm_tcp_socket_client::send_data(const uint8_t *const in, const size_t n)
{
	const uint8_t *p   = in;
	size_t         len = n;

	while(len > 0) {
		my_unique_lock lck(&cfd_lock);
#if defined(_WIN32)
		int rc = send(cfd, reinterpret_cast<const char *>(p), len, 0);
		if (rc <= 0) {
			DOLOG(log_ss::LS_COMM, "comm_tcp_socket_client::send_data: failed");
			closesocket(cfd);
			cfd = INVALID_SOCKET;
			break;
		}
#else
		int rc = write(cfd, p, len);
		if (rc <= 0) {
			DOLOG(log_ss::LS_COMM, "comm_tcp_socket_client::send_data: failed");
			close(cfd);
			cfd = INVALID_SOCKET;
			break;
		}
#endif

		p   += rc;
		len -= rc;
	}
}

void comm_tcp_socket_client::operator()()
{
	set_thread_name("kek:COMMTCPC");

	DOLOG(log_ss::LS_COMM, "TCP comm (client) thread started for %s:%d", host.c_str(), port);

	while(!stop_flag) {
		myusleep(101000l);
		my_unique_lock lck(&cfd_lock);
		if  (cfd != INVALID_SOCKET)
			continue;

		addrinfo *res     = nullptr;

		addrinfo hints { };
		hints.ai_family   = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		char port_str[8] { 0 };
		snprintf(port_str, sizeof port_str, "%u", port);

		int rc = getaddrinfo(host.c_str(), port_str, &hints, &res);
		if (rc != 0) {
#ifdef ESP32
			DOLOG(log_ss::LS_COMM, "comm_tcp_socket_client: cannot resolve \"%s\":%s", host.c_str(), port_str);
#else
			DOLOG(log_ss::LS_COMM, "comm_tcp_socket_client: cannot resolve \"%s\":%s: %s", host.c_str(), port_str, gai_strerror(rc));
#endif

			continue;
		}

		for(addrinfo *p = res; p != NULL; p = p->ai_next) {
			if ((cfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
				continue;

			if (::connect(cfd, p->ai_addr, p->ai_addrlen) == -1) {
				closesocket(cfd);
				cfd = INVALID_SOCKET;
				DOLOG(log_ss::LS_COMM, "comm_tcp_socket_client: cannot connect");
				continue;
			}

			break;
		}

		freeaddrinfo(res);

		if (cfd != INVALID_SOCKET)
			set_nodelay(cfd);
	}

	DOLOG(log_ss::LS_COMM, "comm_tcp_socket_client thread terminating");

	closesocket(cfd);
}

#if IS_POSIX
JsonDocument comm_tcp_socket_client::serialize() const
{
	JsonDocument j;

	j["comm-backend-type"] = "tcp-client";

	j["host"] = host;
	j["port"] = port;

	return j;
}

comm_tcp_socket_client *comm_tcp_socket_client::deserialize(const JsonVariantConst j)
{
	comm_tcp_socket_client *r = new comm_tcp_socket_client(j["host"].as<std::string>(), j["port"].as<int>());

	return r;
}
#endif
