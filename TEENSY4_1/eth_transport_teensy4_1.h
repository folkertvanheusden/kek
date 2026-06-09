#include "gen.h"
#include <cstdint>
#include <string>

#include "eth_transport.h"
#include "my_lock.h"


class eth_transport_teensy4_1: public eth_transport
{
public:
	eth_transport_teensy4_1(const uint8_t mac[6]);
	virtual ~eth_transport_teensy4_1();

	bool begin() override;

	void set_trace(const bool state) override;

	std::string identifier() const override;

	bool transmit(const uint8_t *const data, const size_t n_bytes) override;
	std::pair<uint8_t *, size_t> get(const int timeout) override;
};
