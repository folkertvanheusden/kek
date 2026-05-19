#include "gen.h"
#include <cstdint>
#include <string>

#include "eth_transport.h"


class eth_transport_teensy4_1: public eth_transport
{
private:
	uint8_t mac_address[6];

public:
	eth_transport_teensy4_1(const uint8_t mac[6]);
	virtual ~eth_transport_teensy4_1();

	bool begin() override;

	std::string identifier() const override;

	void transmit(const uint8_t *const data, const size_t n_bytes) override;
	std::pair<uint8_t *, size_t> get(const int timeout) override;
};
