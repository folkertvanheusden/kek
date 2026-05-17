// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include <vector>

#include "comm.h"
#include "console_comm.h"


class console_esp32 : public console_comm
{
protected:
	int wait_for_char_ll(const short timeout) override;
	void put_char_ll    (const char c) override;

public:
	console_esp32(kek_event_t *const stop_event, comm *const io_port, const int t_width, const int t_height);
	virtual ~console_esp32();

	void set_panel_mode(const panel_mode_t pm);

	void put_string_lf(const std::string & what) override;

	void resize_terminal() override;

	void refresh_virtual_terminal() override;

	void panel_update_thread() override;
};
