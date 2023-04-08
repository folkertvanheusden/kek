// (C) 2018-2023 by Folkert van Heusden
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

public:
	disk_backend_esp32(const std::string & filename);
	virtual ~disk_backend_esp32();

	bool begin() override;

	bool read(const off_t offset, const size_t n, uint8_t *const target) override;

	bool write(const off_t offset, const size_t n, const uint8_t *const from) override;
};
