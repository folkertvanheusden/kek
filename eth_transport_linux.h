#include "gen.h"
#if IS_POSIX
#include <cstdint>
#include <string>
#include "eth_transport.h"


class eth_transport_linux: public eth_transport
{
private:
	const std::string dev_name;
	int               fd       { -1 };

public:
	eth_transport_linux(const std::string & dev_name);
	virtual ~eth_transport_linux();

	bool begin() override;

	std::string identifier() const override;

	bool transmit(const uint8_t *const data, const size_t n_bytes) override;
	std::pair<uint8_t *, size_t> get(const int timeout) override;
};
#endif
