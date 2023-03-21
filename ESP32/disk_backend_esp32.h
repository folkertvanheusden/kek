#include <string>

#include "disk_backend.h"
#include "esp32.h"


class disk_backend_esp32 : public disk_backend
{
private:
	const std::string filename;
	File32     *const fh { nullptr };

public:
	disk_backend_esp32(const std::string & filename);
	virtual ~disk_backend_esp32();

	bool begin();

	bool read(const off_t offset, const size_t n, uint8_t *const target);

	bool write(const off_t offset, const size_t n, const uint8_t *const from);
};
