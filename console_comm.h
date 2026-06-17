// (C) 2026 by Folkert van Heusden
// Released under MIT license

#include <vector>

#include "comm.h"
#include "console.h"


class console_comm : public console
{
protected:
	comm         *const io_port    { nullptr };

	int wait_for_char_ll(const int  timeout) override;
	void put_char_ll    (const char c      ) override;

public:
	console_comm(kek_event_t *const stop_event, comm *const io_port, const int t_width, const int t_height);
	virtual ~console_comm();

	void panel_update_thread() override;

	void put_string_lf(const std::string & what) override;

	void resize_terminal() override;
	void refresh_virtual_terminal() override;

	void ui_event_loop() override;
};
