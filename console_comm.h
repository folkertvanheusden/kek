// (C) 2026 by Folkert van Heusden
// Released under MIT license

#include <vector>

#include "comm.h"
#include "console.h"


class console_comm : public console
{
public:
	enum panel_mode_t { PM_BITS, PM_POINTER };

protected:
	comm         *const io_port    { nullptr };
	panel_mode_t        panel_mode { PM_BITS };  // TODO: atomic_int or locking (altough int...)

	int wait_for_char_ll(const short timeout) override;
	void put_char_ll    (const char c) override;

public:
	console_comm(kek_event_t *const stop_event, comm *const io_port, const int t_width, const int t_height);
	virtual ~console_comm();

	void set_panel_mode(const panel_mode_t pm);
	void panel_update_thread() override;

	void put_string_lf(const std::string & what) override;

	void resize_terminal() override;
	void refresh_virtual_terminal() override;
};
