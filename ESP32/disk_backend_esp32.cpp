// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <fcntl.h>
#include <unistd.h>

#include "disk_backend_esp32.h"
#include "error.h"
#include "log.h"


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

bool disk_backend_esp32::begin()
{
	if (!fh->open(filename.c_str(), O_RDWR)) {
		DOLOG(ll_error, true, "rk05: cannot open \"%s\"", filename.c_str());

		return false;
	}

	return true;
}

bool disk_backend_esp32::read(const off_t offset, const size_t n, uint8_t *const target)
{
	DOLOG(debug, false, "disk_backend_esp32::read: read %zu bytes from offset %zu", n, offset);

#if defined(ESP32)
	digitalWrite(LED_BUILTIN, LOW);
#endif

	if (!fh->seek(offset))
		DOLOG(ll_error, true, "seek error %s", strerror(errno));

	yield();

	if (fh->read(target, n) != size_t(n)) {
       		DOLOG(debug, true, "fread error: %s", strerror(errno));

#if defined(ESP32)
		digitalWrite(LED_BUILTIN, HIGH);
#endif
		return false;
	}

	yield();

#if defined(ESP32)
	digitalWrite(LED_BUILTIN, HIGH);
#endif

	return true;
}

bool disk_backend_esp32::write(const off_t offset, const size_t n, const uint8_t *const from)
{
	DOLOG(debug, false, "disk_backend_esp32::write: write %zu bytes to offset %zu", n, offset);

#if defined(ESP32)
	digitalWrite(LED_BUILTIN, LOW);
#endif

	if (!fh->seek(offset))
		DOLOG(ll_error, true, "seek error %s", strerror(errno));

	yield();

	if (fh->write(from, n) != n) {
		DOLOG(ll_error, true, "RK05 fwrite error %s", strerror(errno));

#if defined(ESP32)
		digitalWrite(LED_BUILTIN, HIGH);
#endif
		return false;
	}

	yield();

#if defined(ESP32)
		digitalWrite(LED_BUILTIN, HIGH);
#endif

	return true;
}
