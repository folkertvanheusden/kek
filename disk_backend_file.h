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

	bool begin();

	bool read(const off_t offset, const size_t n, uint8_t *const target);

	bool write(const off_t offset, const size_t n, const uint8_t *const from);
};
