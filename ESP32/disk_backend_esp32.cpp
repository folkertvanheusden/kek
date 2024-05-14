// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <fcntl.h>
#include <unistd.h>

#include "disk_backend_esp32.h"
#include "error.h"
#include "log.h"


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

JsonVariant disk_backend_esp32::serialize() const
{
	JsonVariant j;

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

#if defined(ESP32) && !defined(SHA2017)
	digitalWrite(LED_BUILTIN, LOW);
#endif

	if (!fh->seek(offset)) {
		DOLOG(ll_error, true, "seek error %s", strerror(errno));
		emit_error();
		return false;
	}

	yield();

	ssize_t rc = fh->read(target, n);
	if (size_t(rc) != n) {
       		DOLOG(ll_error, true, "fread error: %s (%zd/%zu)", strerror(errno), rc, n);
		emit_error();

#if defined(ESP32) && !defined(SHA2017)
		digitalWrite(LED_BUILTIN, HIGH);
#endif
		return false;
	}

	yield();

#if defined(ESP32) && !defined(SHA2017)
	digitalWrite(LED_BUILTIN, HIGH);
#endif

	return true;
}

bool disk_backend_esp32::write(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size)
{
	DOLOG(debug, false, "disk_backend_esp32::write: write %zu bytes to offset %zu", n, offset);

#if defined(ESP32) && !defined(SHA2017)
	digitalWrite(LED_BUILTIN, LOW);
#endif

	if (!fh->seek(offset)) {
		DOLOG(ll_error, true, "seek error %s", strerror(errno));
		emit_error();
		return false;
	}

	yield();

	ssize_t rc = fh->write(from, n);
	if (size_t(rc) != n) {
		DOLOG(ll_error, true, "RK05 fwrite error %s (%zd/%zu)", strerror(errno), rc, n);
		emit_error();

#if defined(ESP32) && !defined(SHA2017)
		digitalWrite(LED_BUILTIN, HIGH);
#endif
		return false;
	}

	yield();

#if defined(ESP32) && !defined(SHA2017)
	digitalWrite(LED_BUILTIN, HIGH);
#endif

	return true;
}
