// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <Arduino.h>
#include <vector>

#include "console.h"


class console_esp32 : public console
{
public:
	enum panel_mode_t { PM_BITS, PM_POINTER };

private:
	std::vector<Stream *> io_ports;
	panel_mode_t          panel_mode { PM_BITS };  // TODO: atomic_int

protected:
	int wait_for_char_ll(const short timeout) override;

	void put_char_ll(const char c) override;

public:
	console_esp32(std::atomic_uint32_t *const stop_event, std::vector<Stream *> & io_ports, const int t_width, const int t_height);
	virtual ~console_esp32();

	void set_panel_mode(const panel_mode_t pm);

	void put_string_lf(const std::string & what) override;

	void resize_terminal() override;

	void refresh_virtual_terminal() override;

	void panel_update_thread() override;
};
