// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#if defined(BUILD_FOR_PICO2W)
#include <Arduino.h>
#include <WiFiClient.h>
#endif
#include <string>
#include <sys/types.h>

#include "disk_backend.h"
#include "gen.h"
#include "utils.h"


class disk_backend_nbd : public disk_backend
{
private:
	const std::string host;
	const unsigned    port {  0 };
#if defined(BUILD_FOR_PICO2W)
	WiFiClient        handle;
#else
	int               fd   { -1 };
#endif

	bool connect(const bool retry);

public:
	disk_backend_nbd(const std::string & host, const unsigned port);
	virtual ~disk_backend_nbd();

	JsonDocument serialize() const override;
	static disk_backend_nbd *deserialize(const JsonVariantConst j);

	std::string get_identifier() const override { return format("%s:%d", host.c_str(), port); }

	bool begin(const bool snapshots) override;

	bool read(const off_t offset, const size_t n, uint8_t *const target, const size_t sector_size) override;

	bool write(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size) override;
};
