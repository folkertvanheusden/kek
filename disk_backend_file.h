// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#if IS_POSIX
#include <ArduinoJson.h>
#endif
#include <string>

#include "disk_backend.h"


class disk_backend_file : public disk_backend
{
private:
	const std::string filename;
	int               fd       { -1 };

public:
	disk_backend_file(const std::string & filename);
	virtual ~disk_backend_file();

#if IS_POSIX
	JsonDocument serialize() override;
	static disk_backend_file *deserialize(const JsonVariantConst j);
#endif

	std::string get_identifier() const override { return "file:" + filename; }
	void show_state(console *const cnsl) const override;

	bool begin(const bool snapshots) override;

	bool read(const off_t offset, const size_t n, uint8_t *const target, const size_t sector_size) override;

	bool write(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size) override;
};
