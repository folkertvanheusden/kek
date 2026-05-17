#include "gen.h"
#include <cstdint>
#include <string>

#include "eth_transport.h"


class eth_transport_esp32: public eth_transport
{
public:
	eth_transport_esp32();
	virtual ~eth_transport_esp32();

	bool begin() override;

	std::string identifier() const override;

	void transmit(const uint8_t *const data, const size_t n_bytes) override;
	std::pair<uint8_t *, size_t> get(const int timeout) override;
};
