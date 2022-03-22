#include <termios.h>

#include "console.h"


class console_posix : public console
{
private:
	struct termios org_tty_opts { 0 };

protected:
	int wait_for_char(const int timeout) override;

public:
	console_posix(std::atomic_bool *const terminate);
	virtual ~console_posix();

	void put_char(const char c) override;

	void resize_terminal() override;
};
