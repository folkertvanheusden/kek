// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include <fcntl.h>
#include <unistd.h>

#include "disk_backend_esp32.h"
#include "error.h"
#include "log.h"

#define RETRY_COUNT 4
bool init_sd();

static SdFat sd;

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

JsonDocument disk_backend_esp32::serialize() const
{
	JsonDocument j;

	j["disk-backend-type"] = "esp32";
        j["overlay"] = serialize_overlay();
        // TODO store checksum of backend
        j["filename"] = filename;

	return j;
}

disk_backend_esp32 *disk_backend_esp32::deserialize(const JsonVariantConst j)
{
	// TODO verify checksum of backend
	return new disk_backend_esp32(j["file"].as<std::string>());
}

void disk_backend_esp32::emit_error()
{
	DOLOG(ll_error, true, "SdFat error: %d/%d", sd.sdErrorCode(), sd.sdErrorData());
}

bool disk_backend_esp32::begin(const bool dummy)
{
	if (!fh->open(filename.c_str(), O_RDWR)) {
		DOLOG(ll_error, true, "rk05: cannot open \"%s\"", filename.c_str());
		emit_error();

		return false;
	}

	return true;
}

bool disk_backend_esp32::read(const off_t offset, const size_t n, uint8_t *const target, const size_t sector_size)
{
	DOLOG(debug, false, "disk_backend_esp32::read: read %zu bytes from offset %zu", n, offset);

	if (!fh->seek(offset)) {
		DOLOG(ll_error, true, "seek error %02x", fh->getError());
		emit_error();
		return false;
	}

	yield();

	for(int i=0; i<RETRY_COUNT; i++) {
		ssize_t rc = fh->read(target, n);
		if (size_t(rc) == n)
			break;
		DOLOG(ll_error, true, "%d] fread error: %02x (%zd/%zu)", i, fh->getError(), rc, n);
		emit_error();
		if (!init_sd())
			DOLOG(ll_error, true, "(re-)init SD failed");
		yield();
	}

	return true;
}

bool disk_backend_esp32::write(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size)
{
	DOLOG(debug, false, "disk_backend_esp32::write: write %zu bytes to offset %zu", n, offset);

	if (!fh->seek(offset)) {
		DOLOG(ll_error, true, "seek error %02x", fh->getError());
		emit_error();
		return false;
	}

	yield();

	for(int i=0; i<RETRY_COUNT; i++) {
		ssize_t rc = fh->write(from, n);
		if (size_t(rc) == n)
			break;
		DOLOG(ll_error, true, "%d] fwrite error %02x (%zd/%zu)", i, fh->getError(), rc, n);
		emit_error();
		if (!init_sd())
			DOLOG(ll_error, true, "(re-)init SD failed");
		yield();
	}

	return true;
}
