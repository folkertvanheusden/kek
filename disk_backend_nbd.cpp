#include <fcntl.h>
#include <unistd.h>

#include "disk_backend_nbd.h"
#include "log.h"

#ifdef ESP32
#include <lwip/sockets.h>
#else
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
	addrinfo *res   = nullptr;

	addrinfo hints { 0 };
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	char port_str[8] { 0 };
	snprintf(port_str, sizeof port_str, "%d", port);

	int rc = getaddrinfo(host.c_str(), port_str, &hints, &res);

	if (rc != 0) {
		DOLOG(ll_error, true, "disk_backend_nbd: cannot resolve \"%s\":%s: %s", host.c_str(), port_str, gai_strerror(rc));

		return false;
	}

	return true;
}

bool disk_backend_nbd::read(const off_t offset, const size_t n, uint8_t *const target)
{
	DOLOG(debug, false, "disk_backend_nbd::read: read %zu bytes from offset %zu", n, offset);

	return pread(fd, target, n, offset) == ssize_t(n);
}

bool disk_backend_nbd::write(const off_t offset, const size_t n, const uint8_t *const from)
{
	DOLOG(debug, false, "disk_backend_nbd::write: write %zu bytes to offset %zu", n, offset);

	return pwrite(fd, from, n, offset) == ssize_t(n);
}
