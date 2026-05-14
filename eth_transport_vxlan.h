#include "gen.h"
#include <cstdint>
#include <string>
#if defined(BUILD_FOR_PICO2W)
#include <WiFiUdp.h>
#endif

#include "eth_transport.h"


class eth_transport_vxlan: public eth_transport
{
private:
	const std::string peer;
	const int         port { 4789 };
	const uint32_t    id   { 0    };
#if defined(BUILD_FOR_PICO2W)
	WiFiUDP   udp;
#else
	int       fd           { -1   };
#endif

public:
	eth_transport_vxlan(const std::string & peer, const int port = 4789, const uint32_t id = 0);
	virtual ~eth_transport_vxlan();

	bool begin() override;

	std::string identifier() const override;

	void transmit(const uint8_t *const data, const size_t n_bytes) override;
	std::pair<uint8_t *, size_t> get(const int timeout) override;
};
