#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "disk_backend_file.h"
#include "log.h"


disk_backend_file::disk_backend_file(const std::string & filename) :
	filename(filename)
{
}

disk_backend_file::~disk_backend_file()
{
	close(fd);
}

bool disk_backend_file::begin()
{
	fd = open(filename.c_str(), O_RDWR);

	if (fd == -1) {
		DOLOG(ll_error, true, "disk_backend_file: cannot open \"%s\": %s", filename.c_str(), strerror(errno));

		return false;
	}

	return true;
}

bool disk_backend_file::read(const off_t offset, const size_t n, uint8_t *const target)
{
	DOLOG(debug, false, "disk_backend_file::read: read %zu bytes from offset %zu", n, offset);

	return pread(fd, target, n, offset) == ssize_t(n);
}

bool disk_backend_file::write(const off_t offset, const size_t n, const uint8_t *const from)
{
	DOLOG(debug, false, "disk_backend_file::write: write %zu bytes to offset %zu", n, offset);

	return pwrite(fd, from, n, offset) == ssize_t(n);
}
