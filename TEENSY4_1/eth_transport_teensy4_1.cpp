#include "gen.h"

#include "eth_transport_teensy4_1.h"
#include "log.h"
#include "utils.h"


constexpr const int     max_pkt_size = 1512;
constexpr const uint8_t bc_addr[] { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
uint8_t                *pkt_temp         { new uint8_t[max_pkt_size] };
uint8_t                *pkt_queue_buffer { new uint8_t[max_pkt_size] };  // 1 entry!
StaticQueue_t          *pkt_queue_meta   { new StaticQueue_t()       };
QueueHandle_t           pkt_queue { xQueueCreateStatic(1, max_pkt_size, pkt_queue_buffer, pkt_queue_meta) };
uint8_t                 mac_me[6] { };
bool                    qnethernet_trace { false };

eth_transport_teensy4_1::eth_transport_teensy4_1(const uint8_t mac[6])
{
	memcpy(mac_me, mac, 6);
}

eth_transport_teensy4_1::~eth_transport_teensy4_1()
{
}

bool eth_transport_teensy4_1::begin()
{
	qn::Ethernet.setMACAddressAllowed(mac_me, true);
	return true;
}

std::string eth_transport_teensy4_1::identifier() const
{
	return "teensy4.1";
}

bool eth_transport_teensy4_1::transmit(const uint8_t *const data, const size_t n_bytes)
{
	return qn::EthernetFrame.send(data, n_bytes);
}

std::pair<uint8_t *, size_t> eth_transport_teensy4_1::get(const int timeout)
{
	uint8_t *out = new uint8_t[max_pkt_size];
	if (xQueueReceive(pkt_queue, out, timeout / portTICK_PERIOD_MS) == pdPASS)
		return { out, max_pkt_size };

	delete [] out;

	return { nullptr, 0 };
}

void eth_transport_teensy4_1::set_trace(const bool state)
{
	eth_transport::set_trace(state);
	qnethernet_trace = state;
}

extern "C" {
bool qnethernet_raw_frame_filter(struct pbuf *p, struct netif *netif)
{
	const eth_hdr *const eh = reinterpret_cast<const eth_hdr *>(p->payload);
	const uint8_t *const pl = reinterpret_cast<const uint8_t *>(&eh->dest );
	const int   len         = p->len - (reinterpret_cast<const uint8_t *>(&eh->dest) - reinterpret_cast<const uint8_t *>(p->payload));

	if (len > 14 && len <= max_pkt_size && (memcmp(pl, mac_me, 6) == 0 || memcmp(pl, bc_addr, 6) == 0)) {
		if (qnethernet_trace)
			Serial.println(format("Pkt to %02x:%02x:%02x:%02x:%02x:%02x processed", 
						pl[0], pl[1], pl[2], pl[3], pl[4], pl[5]).c_str());
		memcpy(pkt_temp, pl, len);  // xQueueSend assumes always max_pkt_size
		xQueueSend(pkt_queue, pkt_temp, 0);
		return memcmp(pl, bc_addr, 6) != 0;  // both should process broadcastst (arp!)
	}

	if (qnethernet_trace)
		Serial.println(format("Pkt to %02x:%02x:%02x:%02x:%02x:%02x *NOT* processed", 
					pl[0], pl[1], pl[2], pl[3], pl[4], pl[5]).c_str());

	return false;
}
}
