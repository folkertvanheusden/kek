// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <string>

#include "disk_backend.h"


class disk_backend_file : public disk_backend
{
private:
	const std::string filename;

	int fd { -1 };

public:
	disk_backend_file(const std::string & filename);
	virtual ~disk_backend_file();

#if IS_POSIX
	json_t *serialize() const override;
	static disk_backend_file *deserialize(const json_t *const j);
#endif

	bool begin() override;

	bool read(const off_t offset, const size_t n, uint8_t *const target) override;

	bool write(const off_t offset, const size_t n, const uint8_t *const from) override;
};
