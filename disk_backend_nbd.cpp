// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "disk_backend_nbd.h"
#include "log.h"
#include "utils.h"

#if defined(ESP32)
#include <lwip/netdb.h>
#include <lwip/sockets.h>
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
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#define HTONLL(x) ((1==htonl(1)) ? (x) : (((uint64_t)htonl((x) & 0xFFFFFFFFUL)) << 32) | htonl((uint32_t)((x) >> 32)))
#define NTOHLL(x) ((1==ntohl(1)) ? (x) : (((uint64_t)ntohl((x) & 0xFFFFFFFFUL)) << 32) | ntohl((uint32_t)((x) >> 32)))


disk_backend_nbd::disk_backend_nbd(const std::string & host, const unsigned port) :
	host(host),
	port(port)
{
}

disk_backend_nbd::~disk_backend_nbd()
{
	close(fd);
}

bool disk_backend_nbd::begin()
{
	if (!connect(false)) {
		DOLOG(ll_error, true, "disk_backend_nbd: cannot connect to NBD server");
		return false;
	}

	DOLOG(info, true, "disk_backend_nbd: connected to NBD server");

	return true;
}

bool disk_backend_nbd::connect(const bool retry)
{
	do {
		// LOOP until connected, logging message, exponential backoff?
		addrinfo *res   = nullptr;

		addrinfo hints { 0 };
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		char port_str[8] { 0 };
		snprintf(port_str, sizeof port_str, "%u", port);

		int rc = getaddrinfo(host.c_str(), port_str, &hints, &res);

		if (rc != 0) {
#ifdef ESP32
			DOLOG(ll_error, true, "disk_backend_nbd: cannot resolve \"%s\":%s", host.c_str(), port_str);
#else
			DOLOG(ll_error, true, "disk_backend_nbd: cannot resolve \"%s\":%s: %s", host.c_str(), port_str, gai_strerror(rc));
#endif

			sleep(1);

			continue;
		}

		for(addrinfo *p = res; p != NULL; p = p->ai_next) {
			if ((fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
				continue;
			}

			if (::connect(fd, p->ai_addr, p->ai_addrlen) == -1) {
				close(fd);
				fd = -1;
				DOLOG(ll_error, true, "disk_backend_nbd: cannot connect");
				continue;
			}

			break;
		}

		freeaddrinfo(res);

		struct __attribute__ ((packed)) {
			uint8_t  magic1[8];
			uint8_t  magic2[8];
			uint64_t size;
			uint32_t flags;
			uint8_t  padding[124];
		} nbd_hello;

		if (fd != -1) {
			if (READ(fd, reinterpret_cast<char *>(&nbd_hello), sizeof nbd_hello) != sizeof nbd_hello) {
				close(fd);
				fd = -1;
				DOLOG(warning, true, "disk_backend_nbd::connect: connect short read");
			}
		}

		if (fd != -1 && memcmp(nbd_hello.magic1, "NBDMAGIC", 8) != 0) {
			close(fd);
			fd = -1;
			DOLOG(warning, true, "disk_backend_nbd::connect: magic invalid");
		}

		if (fd != -1)
			DOLOG(info, false, "NBD size: %u", NTOHLL(nbd_hello.size));
	}
	while(fd == -1 && retry);

	return fd != -1;
}

bool disk_backend_nbd::read(const off_t offset, const size_t n, uint8_t *const target)
{
	DOLOG(debug, false, "disk_backend_nbd::read: read %zu bytes from offset %zu", n, offset);

	if (n == 0)
		return true;

	do {
		if (fd == -1 && !connect(true)) {
			DOLOG(warning, true, "disk_backend_nbd::read: (re-)connect");
			sleep(1);
			continue;
		}

		struct __attribute__ ((packed)) {
			uint32_t magic;
			uint32_t type;
			uint64_t handle;
			uint64_t offset;
			uint32_t length;
		} nbd_request { 0 };

		nbd_request.magic  = ntohl(0x25609513);
		nbd_request.type   = 0;  // READ
		nbd_request.offset = HTONLL(uint64_t(offset));
		nbd_request.length = htonl(n);

		if (WRITE(fd, reinterpret_cast<const char *>(&nbd_request), sizeof nbd_request) != sizeof nbd_request) {
			DOLOG(warning, true, "disk_backend_nbd::read: problem sending request");
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}

		struct __attribute__ ((packed)) {
			uint32_t magic;
			uint32_t error;
			uint64_t handle;
		} nbd_reply;

		if (READ(fd, reinterpret_cast<char *>(&nbd_reply), sizeof nbd_reply) != sizeof nbd_reply) {
			DOLOG(warning, true, "disk_backend_nbd::read: problem receiving reply header");
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}

		if (ntohl(nbd_reply.magic) != 0x67446698) {
			DOLOG(warning, true, "disk_backend_nbd::read: bad reply header %08x", nbd_reply.magic);
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}

		int error = ntohl(nbd_reply.error);
		if (error) {
			DOLOG(warning, true, "disk_backend_nbd::read: NBD server indicated error: %d", error);
			return false;
		}

		if (READ(fd, reinterpret_cast<char *>(target), n) != ssize_t(n)) {
			DOLOG(warning, true, "disk_backend_nbd::read: problem receiving payload");
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}
	}
	while(fd == -1);

	return true;
}

bool disk_backend_nbd::write(const off_t offset, const size_t n, const uint8_t *const from)
{
	DOLOG(debug, false, "disk_backend_nbd::write: write %zu bytes to offset %zu", n, offset);

	if (n == 0)
		return true;

	do {
		if (!connect(true)) {
			DOLOG(warning, true, "disk_backend_nbd::write: (re-)connect");
			sleep(1);
			continue;
		}

		struct __attribute__ ((packed)) {
			uint32_t magic;
			uint32_t type;
			uint64_t handle;
			uint64_t offset;
			uint32_t length;
		} nbd_request { 0 };

		nbd_request.magic  = ntohl(0x25609513);
		nbd_request.type   = 1;  // WRITE
		nbd_request.offset = HTONLL(uint64_t(offset));
		nbd_request.length = htonl(n);

		if (WRITE(fd, reinterpret_cast<const char *>(&nbd_request), sizeof nbd_request) != sizeof nbd_request) {
			DOLOG(warning, true, "disk_backend_nbd::write: problem sending request");
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}

		if (WRITE(fd, reinterpret_cast<const char *>(from), n) != ssize_t(n)) {
			DOLOG(warning, true, "disk_backend_nbd::write: problem sending payload");
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}

		struct __attribute__ ((packed)) {
			uint32_t magic;
			uint32_t error;
			uint64_t handle;
		} nbd_reply;

		if (READ(fd, reinterpret_cast<char *>(&nbd_reply), sizeof nbd_reply) != sizeof nbd_reply) {
			DOLOG(warning, true, "disk_backend_nbd::write: problem receiving reply header");
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}

		if (ntohl(nbd_reply.magic) != 0x67446698) {
			DOLOG(warning, true, "disk_backend_nbd::write: bad reply header %08x", nbd_reply.magic);
			close(fd);
			fd = -1;
			sleep(1);
			continue;
		}

		int error = ntohl(nbd_reply.error);
		if (error) {
			DOLOG(warning, true, "disk_backend_nbd::write: NBD server indicated error: %d", error);
			return false;
		}
	}
	while(fd == -1);

	return true;
}
