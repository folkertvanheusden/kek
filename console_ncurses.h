#include <ncurses.h>

#include "console.h"
#include "terminal.h"


class console_ncurses : public console
{
private:
	NEWWIN *w_main_b     { nullptr };
	NEWWIN *w_main       { nullptr };

protected:
	std::optional<char> wait_for_char(const int timeout) override;

public:
	console_ncurses(std::atomic_bool *const terminate);
	virtual ~console_ncurses();

	void put_char(const char c) override;

	void resize_terminal() override;
};
