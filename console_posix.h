#include <termios.h>

#include "console.h"


class console_posix : public console
{
private:
	struct termios org_tty_opts { 0 };

protected:
	int wait_for_char(const short timeout) override;

	void put_char_ll(const char c) override;

public:
	console_posix(std::atomic_bool *const terminate, bus *const b);
	virtual ~console_posix();

	void start_thread() override;

	void resize_terminal() override;

	void put_string_lf(const std::string & what) override;

	void refresh_virtual_terminal() override;

	void panel_update_thread() override;
};
