// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

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

#if defined(_WIN32) // hope for the best
	if (lseek(fd, offset, SEEK_SET) == -1)
		return false;

	return ::read(fd, target, n) == ssize_t(n);
#else
	return pread(fd, target, n, offset) == ssize_t(n);
#endif
}

bool disk_backend_file::write(const off_t offset, const size_t n, const uint8_t *const from)
{
	DOLOG(debug, false, "disk_backend_file::write: write %zu bytes to offset %zu", n, offset);

#if defined(_WIN32) // hope for the best
	if (lseek(fd, offset, SEEK_SET) == -1)
		return false;

	return ::write(fd, from, n) == ssize_t(n);
#else
	return pwrite(fd, from, n, offset) == ssize_t(n);
#endif
}
