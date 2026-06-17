// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#if defined(_WIN32)
#include <windows.h>
#else
#include <termios.h>
#endif

#include "console.h"


class console_posix : public console
{
private:
#if defined(_WIN32)
	HANDLE h_in          { };
	DWORD  original_mode { };
#else
	struct termios org_tty_opts { };
#endif

protected:
	int  wait_for_char_ll(const int  timeout) override;
	void put_char_ll     (const char c      ) override;

public:
	console_posix(std::atomic_uint32_t *const stop_event);
	virtual ~console_posix();

	void begin() override;

	void resize_terminal() override;

	void put_string_lf(const std::string & what) override;

	void refresh_virtual_terminal() override;
	void panel_update_thread() override;

	void ui_event_loop() override;
};
