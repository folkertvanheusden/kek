// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include "comm.h"
#include "log.h"


class comm_esp32_hardwareserial: public comm
{
private:
	const int uart_nr {  1    };
	const int rx_pin  { -1    };
	const int tx_pin  { -1    };
	const int bitrate { 38400 };

public:
	comm_esp32_hardwareserial(const int uart_nr, const int rx_pin, const int tx_pin, const int bps);
	virtual ~comm_esp32_hardwareserial();

	bool    begin() override;

	JsonDocument serialize() const override;
	static comm_esp32_hardwareserial *deserialize(const JsonVariantConst j);

	std::string get_identifier() const;

	bool    is_connected() override;

	bool    has_data() override;
	uint8_t get_byte() override;

	void    send_data(const uint8_t *const in, const size_t n) override;
};
