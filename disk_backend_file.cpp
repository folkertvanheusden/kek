// (C) 2018-2024 by Folkert van Heusden
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

#if IS_POSIX
json_t *disk_backend_file::serialize() const
{
	json_t *j = json_object();

	json_object_set(j, "disk-backend-type", json_string("file"));

	// TODO store checksum of backend
	json_object_set(j, "filename", json_string(filename.c_str()));

	return j;
}

disk_backend_file *disk_backend_file::deserialize(const json_t *const j)
{
	// TODO verify checksum of backend
	return new disk_backend_file(json_string_value(json_object_get(j, "filename")));
}
#endif

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
	ssize_t rc = pread(fd, target, n, offset);
	if (rc != ssize_t(n)) {
		DOLOG(warning, false, "disk_backend_file::read: read failure. expected %zu bytes, got %zd", n, rc);
		return false;
	}

	return true;
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
	ssize_t rc = pwrite(fd, from, n, offset);
	if (rc != ssize_t(n)) {
		DOLOG(warning, false, "disk_backend_file::write: write failure. expected %zu bytes, got %zd", n, rc);
		return false;
	}

	return true;
#endif
}
