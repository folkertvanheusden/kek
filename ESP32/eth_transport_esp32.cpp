#include "gen.h"

#include "eth_transport_esp32.h"
#include "log.h"
#include "utils.h"


constexpr const int max_pkt_size = 1512;

eth_transport_esp32::eth_transport_esp32()
{
}

eth_transport_esp32::~eth_transport_esp32()
{
}

bool eth_transport_esp32::begin()
{
	return true;
}

std::string eth_transport_esp32::identifier() const
{
	return "esp32";
}

void eth_transport_esp32::transmit(const uint8_t *const data, const size_t n_bytes)
{
}

std::pair<uint8_t *, size_t> eth_transport_esp32::get(const int timeout)
{
	return { nullptr, 0 };
}
