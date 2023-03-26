// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <string>
#include <sys/types.h>

#include "disk_backend.h"


class disk_backend_nbd : public disk_backend
{
private:
	const std::string host;
	const int         port {  0 };
	int               fd   { -1 };

	bool connect(const bool retry);

public:
	disk_backend_nbd(const std::string & host, const int port);
	virtual ~disk_backend_nbd();

	bool begin() override;

	bool read(const off_t offset, const size_t n, uint8_t *const target) override;

	bool write(const off_t offset, const size_t n, const uint8_t *const from) override;
};
