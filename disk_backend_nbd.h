#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>

#include "disk_backend.h"


class disk_backend_nbd : public disk_backend
{
private:
	const std::string host;
	const int         port;
	int               fd   { -1 };

public:
	disk_backend_nbd(const std::string & host, const int port);
	virtual ~disk_backend_nbd();

	bool begin() override;

	bool read(const off_t offset, const size_t n, uint8_t *const target) override;

	bool write(const off_t offset, const size_t n, const uint8_t *const from) override;
};
