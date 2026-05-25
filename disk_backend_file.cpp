// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include <cassert>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "disk_backend_file.h"
#include "gen.h"
#include "log.h"


disk_backend_file::disk_backend_file(const std::string & filename) :
	filename(filename)
{
}

disk_backend_file::~disk_backend_file()
{
	close(fd);
}

void disk_backend_file::show_state(console *const cnsl) const
{
	cnsl->put_string_lf("identifier: " + get_identifier());
}

JsonDocument disk_backend_file::serialize() const
{
	JsonDocument j;

	j["disk-backend-type"] = "file";
	j["overlay"] = serialize_overlay();
	// TODO store checksum of backend
	j["filename"] = filename;

	return j;
}

disk_backend_file *disk_backend_file::deserialize(const JsonVariantConst j)
{
	// TODO verify checksum of backend
	// TODO overlay
	return new disk_backend_file(j["filename"].as<std::string>());
}

bool disk_backend_file::begin(const bool snapshots)
{
#if IS_POSIX
	use_overlay = snapshots;
#endif
#if defined(_WIN32)
	fd = open(filename.c_str(), O_RDWR | O_BINARY);
#else
	fd = open(filename.c_str(), O_RDWR);
#endif
	if (fd == -1) {
		DOLOG(log_ss::LS_DISK, "disk_backend_file: cannot open \"%s\": %s", filename.c_str(), strerror(errno));
		return false;
	}

	return true;
}

bool disk_backend_file::read(const off_t offset_in, const size_t n, uint8_t *const target, const size_t sector_size)
{
	DOLOG(log_ss::LS_DISK, "disk_backend_file::read: read %zu bytes from offset %zu", n, offset_in);

	assert((offset_in % sector_size) == 0);
	assert((n % sector_size) == 0);

	for(off_t o=0; o<off_t(n); o+=sector_size) {
		off_t offset = offset_in + o;
#if IS_POSIX
		auto o_rc = get_from_overlay(offset, sector_size);
		if (o_rc.has_value()) {
			memcpy(&target[o], o_rc.value().data(), sector_size);
			continue;
		}
#endif

#if defined(_WIN32) // hope for the best
		if (lseek(fd, offset, SEEK_SET) == -1)
			return false;

		 if (ssize_t rc = ::read(fd, target, n); rc != ssize_t(n))
			return false;
#else
		ssize_t rc = pread(fd, target, n, offset);
		if (rc != ssize_t(n)) {
			DOLOG(log_ss::LS_DISK, "disk_backend_file::read: read failure. expected %zu bytes, got %zd", n, rc);
			return false;
		}
#endif
	}

	return true;
}

bool disk_backend_file::write(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size)
{
	DOLOG(log_ss::LS_DISK, "disk_backend_file::write: write %zu bytes to offset %zu", n, offset);

#if IS_POSIX
	if (store_mem_range_in_overlay(offset, n, from, sector_size))
		return true;
#endif

#if defined(_WIN32) // hope for the best
	if (lseek(fd, offset, SEEK_SET) == -1)
		return false;

	return ::write(fd, from, n) == ssize_t(n);
#else
	ssize_t rc = pwrite(fd, from, n, offset);
	if (rc != ssize_t(n)) {
		DOLOG(log_ss::LS_DISK, "disk_backend_file::write: write failure. expected %zu bytes, got %zd", n, rc);
		return false;
	}

	return true;
#endif
}
