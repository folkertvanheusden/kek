// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <mutex>
#include <ncurses.h>

#include "console.h"
#include "terminal.h"


class console_ncurses : public console
{
private:
	NEWWIN      *w_main_b  { nullptr };
	NEWWIN      *w_main    { nullptr };
	NEWWIN      *w_panel_b { nullptr };
	NEWWIN      *w_panel   { nullptr };

	std::mutex   ncurses_mutex;

	std::thread *th_panel  { nullptr };

	int          tx        { 0 };
	int          ty        { 0 };

protected:
	int wait_for_char_ll(const short timeout) override;

	void put_char_ll(const char c) override;

public:
	console_ncurses(std::atomic_uint32_t *const stop_event);
	virtual ~console_ncurses();

	void put_string_lf(const std::string & what) override;

	void resize_terminal() override;

	void refresh_virtual_terminal() override;

	void panel_update_thread() override;
};
