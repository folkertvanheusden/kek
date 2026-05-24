#include "gen.h"

#include "console.h"
#include "eth_transport_esp32.h"
#include "log.h"
#include "utils.h"


constexpr const int max_pkt_size = 1512;
static uint8_t buffer[max_pkt_size];

eth_transport_esp32::eth_transport_esp32(const uint8_t mac[6])
{
	memcpy(mac_addr, mac, 6);
}

eth_transport_esp32::~eth_transport_esp32()
{
	delete w5500_instance;
}

bool eth_transport_esp32::begin()
{
	return w5500_instance->begin(mac_addr);
}

std::string eth_transport_esp32::identifier() const
{
	return "esp32+w5500";
}

void eth_transport_esp32::show_state(console *const cnsl) const
{
	eth_transport::show_state(cnsl);
	auto link_state = w5500_instance->wizphy_getphylink();
	cnsl->put_string_lf(format("Link state: %d", link_state));
}

bool eth_transport_esp32::transmit(const uint8_t *const data, const size_t n_bytes)
{
	auto rc = w5500_instance->sendFrame(data, n_bytes);
	pkt_cnt_tx++;
	return rc == n_bytes;
}

std::pair<uint8_t *, size_t> eth_transport_esp32::get(const int timeout)
{
	uint8_t *pkt         = nullptr;
	size_t   packet_size = 0;
	auto     start       = millis();
	int      sleep_n_ms  = 1;
	while(millis() - start < timeout) {
		int rc = w5500_instance->readFrame(buffer, sizeof buffer);
		if (rc > 0) {
			pkt_cnt_rx++;
			pkt = new uint8_t[rc];
			if (!pkt) {
				DOLOG(ll_critical, true, "malloc issue");
				return { nullptr, 0 };
			}
			memcpy(pkt, buffer, rc);
			packet_size = rc;
			break;
		}
		else if (rc == -1) {
			DOLOG(debug, false, "receive error");
		}
		vTaskDelay(sleep_n_ms / portTICK_PERIOD_MS);
		if (sleep_n_ms < 64)
			sleep_n_ms <<= 1;
	}
	return { pkt, packet_size };
}
