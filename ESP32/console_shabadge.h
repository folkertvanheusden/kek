// (C) 2023 by Folkert van Heusden
// Released under MIT license

#include <Arduino.h>
#include <epd2in9-badge.h>
#include <epdpaint.h>
#include <vector>

#include "console_esp32.h"


class console_shabadge : public console_esp32
{
private:
	unsigned char image[4736];
	Paint        *paint { nullptr };
	Epd           epd;

	std::atomic_bool screen_updated { false };

	void put_char_ll(const char c) override;

public:
	console_shabadge(std::atomic_uint32_t *const stop_event, bus *const b, std::vector<Stream *> & io_ports);
	virtual ~console_shabadge();

	void panel_update_thread() override;
};
