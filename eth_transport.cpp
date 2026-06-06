#include <cinttypes>

#include "console.h"
#include "eth_transport.h"


eth_transport::eth_transport()
{
}

eth_transport::~eth_transport()
{
}

FLASHMEM void eth_transport::show_state(console *const cnsl) const
{
	cnsl->put_string_lf(format("%s packets received   : %" PRIu64, identifier().c_str(), pkt_cnt_rx));
	cnsl->put_string_lf(format("%s packets transmitted: %" PRIu64, identifier().c_str(), pkt_cnt_tx));
}
