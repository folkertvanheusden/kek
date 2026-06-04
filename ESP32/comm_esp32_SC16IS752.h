// (C) 2025-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include "comm.h"
#include "log.h"

#include <SC16IS752.h>


class comm_esp32_SC16IS752: public comm
{
private:
	SC16IS752 *parent      { nullptr };
	int        dev_nr      { -1      };
	int        port_nr     { -1      };
	bool       initialized { false   };
	int        baud_rate   { -1      };

public:
	comm_esp32_SC16IS752(SC16IS752 *const parent, const int dev_nr, const int port_nr);
	virtual ~comm_esp32_SC16IS752();

	bool    begin() override;
	bool    need_dealloc() override { return false; }

	void    configure_port(const int baud_rate);

#if IS_POSIX
	JsonDocument serialize() const override;
	static comm_esp32_SC16IS752 *deserialize(const JsonVariantConst j, SC16IS752 *const a, SC16IS752 *const b);
#endif

	std::string get_identifier() const;

	bool    is_connected() override;

	bool    has_data() override;
	uint8_t get_byte() override;

	void    send_data(const uint8_t *const in, const size_t n) override;
};
