// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <Arduino.h>
#include <vector>

#include "console.h"


class console_esp32 : public console
{
private:
	std::vector<Stream *> io_ports;

protected:
	int wait_for_char_ll(const short timeout) override;

	void put_char_ll(const char c) override;

public:
	console_esp32(std::atomic_uint32_t *const stop_event, bus *const b, std::vector<Stream *> & io_ports, const int t_width, const int t_height);
	virtual ~console_esp32();

	void put_string_lf(const std::string & what) override;

	void resize_terminal() override;

	void refresh_virtual_terminal() override;

	void panel_update_thread() override;
};
