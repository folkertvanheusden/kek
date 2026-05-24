#include "gen.h"
#include <cstdint>
#include <string>

#include "eth_transport.h"
#include "my_lock.h"
#include "w5500.h"


class eth_transport_esp32: public eth_transport
{
private:
	uint8_t     mac_addr[6]    {   };
	Wiznet5500 *w5500_instance { new Wiznet5500(13, 12, 11, 14) };
	my_lock     w5500_lock;

public:
	eth_transport_esp32(const uint8_t mac_addr[6]);
	virtual ~eth_transport_esp32();

	bool begin() override;

	std::string identifier() const override;
	void show_state(console *const cnsl) const override;

	bool transmit(const uint8_t *const data, const size_t n_bytes) override;
	std::pair<uint8_t *, size_t> get(const int timeout) override;
};
