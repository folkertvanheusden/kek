#include "gen.h"

#include "eth_transport_teensy4_1.h"
#include "log.h"
#include "utils.h"


constexpr const int max_pkt_size = 1512;

eth_transport_teensy4_1::eth_transport_teensy4_1(const uint8_t mac[6])
{
	memcpy(mac_address, mac, 6);
}

eth_transport_teensy4_1::~eth_transport_teensy4_1()
{
}

bool eth_transport_teensy4_1::begin()
{
	qn::Ethernet.setMACAddressAllowed(mac_address, true);
	return true;
}

std::string eth_transport_teensy4_1::identifier() const
{
	return "teensy4.1";
}

void eth_transport_teensy4_1::transmit(const uint8_t *const data, const size_t n_bytes)
{
	qn::EthernetFrame.send(data, n_bytes);
}

std::pair<uint8_t *, size_t> eth_transport_teensy4_1::get(const int timeout)
{
	auto start = millis();

	do {
		if (auto rc = qn::EthernetFrame.parseFrame(); rc > 0) {
			size_t size = qn::EthernetFrame.size();
			if (size <= 14)
				continue;

			uint8_t *out = new uint8_t[size];
			memcpy(out, qn::EthernetFrame.data(), size);
			return { out, size };
		}

		vTaskDelay(10 / portTICK_PERIOD_MS);  // TODO
	}
	while(millis() < start + timeout);

	return { nullptr, 0 };
}
