// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <fcntl.h>
#include <unistd.h>

#include "disk_backend_esp32.h"
#include "error.h"
#include "log.h"

#define RETRY_COUNT 4
bool init_sd();

#if !defined(TEENSY4_1) && !defined(BUILD_FOR_PICO2W)
extern SdFs SDinstance;
#endif

disk_backend_esp32::disk_backend_esp32(const std::string & filename) :
	filename(filename),
	fh(new File32())
{
}

disk_backend_esp32::~disk_backend_esp32()
{
	fh->close();

	delete fh;
}

void disk_backend_esp32::show_state(console *const cnsl) const
{
	cnsl->put_string_lf("identifier: " + get_identifier());
	cnsl->put_string_lf(format("offset: %u", fh->curPosition()));
}

JsonDocument disk_backend_esp32::serialize()
{
	JsonDocument j;
#if !defined(TEENSY4_1) && !defined(BUILD_FOR_PICO2W)
	j["disk-backend-type"] = "esp32";
        j["overlay"] = serialize_overlay();
        // TODO store checksum of backend
        j["filename"] = filename;
#endif
	return j;
}

disk_backend_esp32 *disk_backend_esp32::deserialize(const JsonVariantConst j)
{
	// TODO verify checksum of backend
	return new disk_backend_esp32(j["file"].as<std::string>());
}

void disk_backend_esp32::emit_error()
{
#if !defined(TEENSY4_1) && !defined(BUILD_FOR_PICO2W)
	DOLOG(log_ss::LS_DISK, "SdFat error: %d/%d", SDinstance.sdErrorCode(), SDinstance.sdErrorData());
#endif
}

bool disk_backend_esp32::begin(const bool dummy)
{
	if (!fh->open(filename.c_str(), O_RDWR)) {
		DOLOG(log_ss::LS_DISK, "rk05: cannot open \"%s\"", filename.c_str());
		emit_error();

		return false;
	}

	return true;
}

bool disk_backend_esp32::read(const off_t offset, const size_t n, uint8_t *const target, const size_t sector_size)
{
	DOLOG(log_ss::LS_DISK, "disk_backend_esp32::read: read %" PRIzu " bytes from offset %" PRIzu "", n, offset);

	if (!fh->seek(offset)) {
		DOLOG(log_ss::LS_DISK, "seek error %02x", fh->getError());
		emit_error();
		return false;
	}

	yield();

	for(int i=0; i<RETRY_COUNT; i++) {
		ssize_t rc = fh->read(target, n);
		if (size_t(rc) == n)
			break;
		DOLOG(log_ss::LS_DISK, "%d] fread error: %02x (%" PRIzd "/%" PRIzu ")", i, fh->getError(), rc, n);
		emit_error();
		if (!init_sd())
			DOLOG(log_ss::LS_DISK, "(re-)init SD failed");
		yield();
	}

	return true;
}

bool disk_backend_esp32::write(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size)
{
	DOLOG(log_ss::LS_DISK, "disk_backend_esp32::write: write %" PRIzu " bytes to offset %" PRIzu "", n, offset);

	if (!fh->seek(offset)) {
		DOLOG(log_ss::LS_DISK, "seek error %02x", fh->getError());
		emit_error();
		return false;
	}

	yield();

	for(int i=0; i<RETRY_COUNT; i++) {
		ssize_t rc = fh->write(from, n);
		if (size_t(rc) == n)
			break;
		DOLOG(log_ss::LS_DISK, "%d] fwrite error %02x (%" PRIzd "/%" PRIzu ")", i, fh->getError(), rc, n);
		emit_error();
		if (!init_sd())
			DOLOG(log_ss::LS_DISK, "(re-)init SD failed");
		yield();
	}

	return true;
}
