#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include "disk_backend_nbd.h"
#include "log.h"

#ifdef ESP32
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif


disk_backend_nbd::disk_backend_nbd(const std::string & host, const int port) :
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
		snprintf(port_str, sizeof port_str, "%d", port);

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
	}
	while(fd == -1 && retry);

	return fd != -1;
}

bool disk_backend_nbd::read(const off_t offset, const size_t n, uint8_t *const target)
{
	DOLOG(debug, false, "disk_backend_nbd::read: read %zu bytes from offset %zu", n, offset);

	connect(true);

	// TODO: loop dat als read() aangeeft dat de peer weg is, dat er dan gereconnect wordt
	// anders return false
	return pread(fd, target, n, offset) == ssize_t(n);
}

bool disk_backend_nbd::write(const off_t offset, const size_t n, const uint8_t *const from)
{
	DOLOG(debug, false, "disk_backend_nbd::write: write %zu bytes to offset %zu", n, offset);

	connect(true);

	// TODO: loop dat als write() aangeeft dat de peer weg is, dat er dan gereconnect wordt
	// anders return false

	return pwrite(fd, from, n, offset) == ssize_t(n);
}
