// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32)
#include <Arduino.h>
#endif
#if defined(ESP32)
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <driver/uart.h>
#elif defined(_WIN32)
// from https://stackoverflow.com/questions/12765743/implicit-declaration-of-function-getaddrinfo-on-mingw
#define _NTDDI_VERSION_FROM_WIN32_WINNT2(ver)    ver##0000
#define _NTDDI_VERSION_FROM_WIN32_WINNT(ver)     _NTDDI_VERSION_FROM_WIN32_WINNT2(ver)

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x501
#endif
#ifndef NTDDI_VERSION
#  define NTDDI_VERSION _NTDDI_VERSION_FROM_WIN32_WINNT(_WIN32_WINNT)
#endif
#include <ws2tcpip.h>
#include <winsock2.h>
#else
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
	std::unique_lock<std::mutex> lck(cfd_lock);

	return cfd != INVALID_SOCKET;
}

bool comm_tcp_socket_client::has_data()
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

uint8_t comm_tcp_socket_client::get_byte()
{
	int use_fd = -1;

	{
		std::unique_lock<std::mutex> lck(cfd_lock);
		use_fd = cfd;
	}

	uint8_t c = 0;
	if (read(use_fd, &c, 1) <= 0) {
		DOLOG(warning, false, "comm_tcp_socket_client::get_byte: failed");
		std::unique_lock<std::mutex> lck(cfd_lock);
		close(cfd);
		cfd = INVALID_SOCKET;
	}

	return c;
}

void comm_tcp_socket_client::send_data(const uint8_t *const in, const size_t n)
{
	const uint8_t *p   = in;
	size_t         len = n;

	while(len > 0) {
		std::unique_lock<std::mutex> lck(cfd_lock);
		int rc = write(cfd, p, len);
		if (rc <= 0) {  // TODO error checking
			DOLOG(warning, false, "comm_tcp_socket_client::send_data: failed");
			close(cfd);
			cfd = INVALID_SOCKET;
			break;
		}

		p   += rc;
		len -= rc;
	}
}

void comm_tcp_socket_client::operator()()
{
	set_thread_name("kek:COMMTCPC");

	DOLOG(info, true, "TCP comm (client) thread started for %s:%d", host.c_str(), port);

	while(!stop_flag) {
		myusleep(101000l);
		std::unique_lock<std::mutex> lck(cfd_lock);
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
			DOLOG(ll_error, true, "comm_tcp_socket_client: cannot resolve \"%s\":%s", host.c_str(), port_str);
#else
			DOLOG(ll_error, true, "comm_tcp_socket_client: cannot resolve \"%s\":%s: %s", host.c_str(), port_str, gai_strerror(rc));
#endif

			myusleep(101000l);
			continue;
		}

		for(addrinfo *p = res; p != NULL; p = p->ai_next) {
			if ((cfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
				continue;

			if (::connect(cfd, p->ai_addr, p->ai_addrlen) == -1) {
				close(cfd);
				cfd = INVALID_SOCKET;
				DOLOG(ll_error, true, "comm_tcp_socket_client: cannot connect");
				continue;
			}

			break;
		}

		freeaddrinfo(res);

		if (cfd != INVALID_SOCKET)
			set_nodelay(cfd);
	}

	DOLOG(info, true, "comm_tcp_socket_client thread terminating");

	close(cfd);
}

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
