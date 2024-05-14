// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <string>

#include "disk_backend.h"
#if defined(BUILD_FOR_RP2040)
#include "rp2040.h"
#else
#include "esp32.h"
#endif


class disk_backend_esp32 : public disk_backend
{
private:
	const std::string filename;
	File32     *const fh { nullptr };

	void emit_error();

public:
	disk_backend_esp32(const std::string & filename);
	virtual ~disk_backend_esp32();

	JsonVariant serialize() const override;
	static disk_backend_esp32 *deserialize(const JsonVariantConst j);

	std::string get_identifier() const { return filename; }

	bool begin(const bool dummy) override;

	bool read(const off_t offset, const size_t n, uint8_t *const target, const size_t sector_size) override;

	bool write(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size) override;
};
