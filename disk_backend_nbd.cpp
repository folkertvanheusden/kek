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
	fd(socket(AF_INET, SOCK_STREAM, 0))
{
	struct addrinfo hints, *res;
	int status;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	char *host = "esp-942daf.local";
	char *port = "5556";
	status = getaddrinfo(host, port, &hints, &res)
}

disk_backend_nbd::~disk_backend_nbd()
{
	close(fd);
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
