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
	int wait_for_char(const int timeout) override;

	void put_char_ll(const char c) override;

public:
	console_ncurses(std::atomic_bool *const terminate, bus *const b);
	virtual ~console_ncurses();

	void resize_terminal() override;

	void panel_update_thread() override;
};
