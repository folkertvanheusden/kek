// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <cassert>
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
#elif defined(BUILD_FOR_PICO2W)
//
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
#if defined(BUILD_FOR_PICO2W)
	handle.stop();
#else
	close(fd);
#endif
}

JsonDocument disk_backend_nbd::serialize() const
{
	JsonDocument j;

	j["disk-backend-type"] = "nbd";

	j["overlay"] = serialize_overlay();

	// TODO store checksum of backend
	j["host"] = host.c_str();
	j["port"] = port;

	return j;
}

disk_backend_nbd *disk_backend_nbd::deserialize(const JsonVariantConst j)
{
	// TODO verify checksum of backend
	return new disk_backend_nbd(j["host"], j["port"]);
}

bool disk_backend_nbd::begin(const bool snapshots)
{
#if IS_POSIX
	use_overlay = snapshots;
#endif
	if (!connect(false)) {
		DOLOG(ll_error, true, "disk_backend_nbd: cannot connect to NBD server");
		return false;
	}

	DOLOG(info, true, "disk_backend_nbd: connected to NBD server");

	return true;
}

#if defined(BUILD_FOR_PICO2W)
int blocking_read(WiFiClient & handle, uint8_t *const to, const size_t n)
{
	while(handle.available() < n && handle.connected()) {
	}

	return handle.read(to, n);
}
#endif

bool disk_backend_nbd::connect(const bool retry)
{
	TRACE("disk_backend_nbd::connect %sretry", retry ? "":"no ");

	struct __attribute__ ((packed)) {
		uint8_t  magic1[8];
		uint8_t  magic2[8];
		uint64_t size;
		uint32_t flags;
		uint8_t  padding[124];
	} nbd_hello { };

#if defined(BUILD_FOR_PICO2W)
	do {
		handle.connect(host.c_str(), port);

		auto start = get_us();
		do {
			vTaskDelay(150 / portTICK_PERIOD_MS);
		}
		while(handle.connected() == false && get_us() - start < 1000000);

		if (handle.connected()) {
			TRACE("disk_backend_nbd::connect nbd_hello");

			handle.setNoDelay(true);

			if (int rc = blocking_read(handle, reinterpret_cast<uint8_t *>(&nbd_hello), sizeof nbd_hello); rc != sizeof(nbd_hello)) {
				handle.stop();
				DOLOG(warning, true, "disk_backend_nbd::connect: connect short read: %d", rc);
				myusleep(101000);
				continue;
			}

			if (memcmp(nbd_hello.magic1, "NBDMAGIC", 8) != 0) {
				handle.stop();
				DOLOG(warning, true, "disk_backend_nbd::connect: magic invalid");
				myusleep(101000);
				continue;
			}
			break;
		}
		else {
			DOLOG(warning, true, "disk_backend_nbd: NOT connected to %s:%d", host.c_str(), port);
			if (retry)
				myusleep(101000);
		}
	}
	while(retry);

	return handle.connected();
#else
	do {
		// LOOP until connected, logging message, exponential backoff?
		addrinfo *res     = nullptr;

		addrinfo hints { };
		hints.ai_family   = AF_INET;
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
			myusleep(101000);
			continue;
		}

		for(addrinfo *p = res; p != NULL; p = p->ai_next) {
			if ((fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
				continue;

			if (::connect(fd, p->ai_addr, p->ai_addrlen) == -1) {
				close(fd);
				fd = -1;
				freeaddrinfo(res);
				DOLOG(ll_error, true, "disk_backend_nbd: cannot connect");
				continue;
			}
			break;
		}
		freeaddrinfo(res);

		if (fd != -1) {
			TRACE("disk_backend_nbd::connect nbd_hello");
			if (READ(fd, reinterpret_cast<char *>(&nbd_hello), sizeof nbd_hello) != sizeof nbd_hello) {
				close(fd);
				fd = -1;
				DOLOG(warning, true, "disk_backend_nbd::connect: connect short read");
			}
		}

		if (fd != -1 && memcmp(nbd_hello.magic1, "NBDMAGIC", 8) != 0) {
			TRACE("disk_backend_nbd::connect NBDMAGIC");
			close(fd);
			fd = -1;
			DOLOG(warning, true, "disk_backend_nbd::connect: magic invalid");
		}

		if (fd != -1) {
			DOLOG(info, false, "NBD size: %u", NTOHLL(nbd_hello.size));
			set_nodelay(fd);
		}
	}
	while(fd == -1 && retry);

	return fd != -1;
#endif
}

bool disk_backend_nbd::read(const off_t offset_in, const size_t n, uint8_t *const target, const size_t sector_size)
{
	TRACE("disk_backend_nbd::read: read %zu bytes from offset %zu", n, offset_in);
	if (n == 0)
		return true;

	size_t o      = 0;
	off_t  offset = offset_in;

	while(offset < offset_in + off_t(n)) {
#if IS_POSIX
		auto o_rc = get_from_overlay(offset, sector_size);
		if (o_rc.has_value()) {
			memcpy(&target[o], o_rc.value().data(), sector_size);
			offset += sector_size;
			o      += sector_size;
			continue;
		}
#endif

#if defined(BUILD_FOR_PICO2W)
		if (handle.connected() == false && !connect(true)) {
			myusleep(101000);
			continue;
		}
#else
		if (fd == -1 && !connect(true)) {
			myusleep(101000);
			continue;
		}
#endif

		struct __attribute__ ((packed)) {
			uint32_t magic;
			uint32_t type;
			uint64_t handle;
			uint64_t offset;
			uint32_t length;
		} nbd_request { };

		nbd_request.magic  = ntohl(0x25609513);
		nbd_request.type   = 0;  // READ
		nbd_request.offset = HTONLL(uint64_t(offset));
		nbd_request.length = htonl(sector_size);

		TRACE("NBD: send READ request");
#if defined(BUILD_FOR_PICO2W)
		if (size_t rc = handle.write(reinterpret_cast<const uint8_t *>(&nbd_request), sizeof nbd_request); rc != sizeof nbd_request) {
			printf("send read req error: %zu\n", rc);
			DOLOG(warning, true, "disk_backend_nbd::read: problem sending request");
			handle.stop();
			myusleep(101000);
			continue;
		}
#else
		if (WRITE(fd, reinterpret_cast<const char *>(&nbd_request), sizeof nbd_request) != sizeof nbd_request) {
			DOLOG(warning, true, "disk_backend_nbd::read: problem sending request");
			close(fd);
			fd = -1;
			myusleep(101000);
			continue;
		}
#endif

		struct __attribute__ ((packed)) {
			uint32_t magic;
			uint32_t error;
			uint64_t handle;
		} nbd_reply;

		TRACE("NBD: receiving READ reply header");
#if defined(BUILD_FOR_PICO2W)
		if (int rc = blocking_read(handle, reinterpret_cast<uint8_t *>(&nbd_reply), sizeof nbd_reply); rc != sizeof nbd_reply) {
			printf("recv read req error: %d %d\n", rc, handle.connected());
			DOLOG(warning, true, "disk_backend_nbd::read: problem receiving reply header");
			handle.stop();
			myusleep(101000);
			continue;
		}
#else
		if (READ(fd, reinterpret_cast<char *>(&nbd_reply), sizeof nbd_reply) != sizeof nbd_reply) {
			DOLOG(warning, true, "disk_backend_nbd::read: problem receiving reply header");
			close(fd);
			fd = -1;
			myusleep(101000);
			continue;
		}
#endif

		if (ntohl(nbd_reply.magic) != 0x67446698) {
			printf("magic invalid\n");
			DOLOG(warning, true, "disk_backend_nbd::read: bad reply header %08x", nbd_reply.magic);
#if defined(BUILD_FOR_PICO2W)
			handle.stop();
#else
			close(fd);
			fd = -1;
#endif
			myusleep(101000);
			continue;
		}

		int error = ntohl(nbd_reply.error);
		if (error) {
			DOLOG(warning, true, "disk_backend_nbd::read: NBD server indicated error: %d", error);
			return false;
		}

		TRACE("NBD: receiving READ reply payload");
#if defined(BUILD_FOR_PICO2W)
		if (int rc = blocking_read(handle, reinterpret_cast<uint8_t *>(target), sector_size); rc != ssize_t(sector_size)) {
			printf("recv payload error %d\r\n", rc);
			DOLOG(warning, true, "disk_backend_nbd::read: problem receiving payload");
			handle.stop();
			myusleep(101000);
			continue;
		}
#else
		if (READ(fd, reinterpret_cast<char *>(target), sector_size) != ssize_t(sector_size)) {
			DOLOG(warning, true, "disk_backend_nbd::read: problem receiving payload");
			close(fd);
			fd = -1;
			myusleep(101000);
			continue;
		}
#endif
		offset += sector_size;
		o      += sector_size;
	}

	return true;
}

bool disk_backend_nbd::write(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size)
{
	TRACE("disk_backend_nbd::write: write %zu bytes to offset %zu", n, offset);

	if (n == 0)
		return true;

#if IS_POSIX
	if (store_mem_range_in_overlay(offset, n, from, sector_size))
		return true;
#endif

	for(;;) {
#if defined(BUILD_FOR_PICO2W)
		if (handle.connected() == false && !connect(true)) {
			DOLOG(warning, true, "disk_backend_nbd::write: (re-)connect");
			myusleep(101000);
			continue;
		}
#else
		if (fd == -1 && !connect(true)) {
			DOLOG(warning, true, "disk_backend_nbd::write: (re-)connect");
			myusleep(101000);
			continue;
		}
#endif

		struct __attribute__ ((packed)) {
			uint32_t magic;
			uint32_t type;
			uint64_t handle;
			uint64_t offset;
			uint32_t length;
		} nbd_request { };

		nbd_request.magic  = ntohl(0x25609513);
		nbd_request.type   = htonl(1);  // WRITE
		nbd_request.offset = HTONLL(uint64_t(offset));
		nbd_request.length = htonl(n);

#if defined(BUILD_FOR_PICO2W)
		if (handle.write(reinterpret_cast<const uint8_t *>(&nbd_request), sizeof nbd_request) != sizeof nbd_request) {
			DOLOG(warning, true, "disk_backend_nbd::write: problem sending request");
			handle.stop();
			myusleep(101000);
			continue;
		}

		if (handle.write(reinterpret_cast<const uint8_t *>(from), n) != ssize_t(n)) {
			DOLOG(warning, true, "disk_backend_nbd::write: problem sending request");
			handle.stop();
			myusleep(101000);
			continue;
		}
#else
		if (WRITE(fd, reinterpret_cast<const char *>(&nbd_request), sizeof nbd_request) != sizeof nbd_request) {
			DOLOG(warning, true, "disk_backend_nbd::write: problem sending request");
			close(fd);
			fd = -1;
			myusleep(101000);
			continue;
		}

		if (WRITE(fd, reinterpret_cast<const char *>(from), n) != ssize_t(n)) {
			DOLOG(warning, true, "disk_backend_nbd::write: problem sending payload");
			close(fd);
			fd = -1;
			myusleep(101000);
			continue;
		}
#endif

		struct __attribute__ ((packed)) {
			uint32_t magic;
			uint32_t error;
			uint64_t handle;
		} nbd_reply;

#if defined(BUILD_FOR_PICO2W)
		if (blocking_read(handle, reinterpret_cast<uint8_t *>(&nbd_reply), sizeof nbd_reply) != sizeof nbd_reply) {
			DOLOG(warning, true, "disk_backend_nbd::read: problem receiving reply header");
			handle.stop();
			myusleep(101000);
			continue;
		}
#else
		if (READ(fd, reinterpret_cast<char *>(&nbd_reply), sizeof nbd_reply) != sizeof nbd_reply) {
			DOLOG(warning, true, "disk_backend_nbd::write: problem receiving reply header");
			close(fd);
			fd = -1;
			myusleep(101000);
			continue;
		}
#endif

		if (ntohl(nbd_reply.magic) != 0x67446698) {
			DOLOG(warning, true, "disk_backend_nbd::write: bad reply header %08x", nbd_reply.magic);
#if defined(BUILD_FOR_PICO2W)
			handle.stop();
#else
			close(fd);
			fd = -1;
#endif
			myusleep(101000);
			continue;
		}

		int error = ntohl(nbd_reply.error);
		if (error) {
			DOLOG(warning, true, "disk_backend_nbd::write: NBD server indicated error: %d", error);
			return false;
		}

		break;
	}

	return true;
}
