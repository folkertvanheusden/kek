#include <string>

#include "disk_backend.h"


class disk_backend_nbd : public disk_backend
{
private:
	const int fd { -1 };

public:
	disk_backend_nbd(const std::string & host, const int port);
	virtual ~disk_backend_nbd();

	bool read(const off_t offset, const size_t n, uint8_t *const target);

	bool write(const off_t offset, const size_t n, const uint8_t *const from);
};
