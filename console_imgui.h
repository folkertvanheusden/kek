// (C) 2026 by Folkert van Heusden
// Released under MIT license

#if defined(USE_IMGUI)
#include <atomic>
#include <thread>

#include "console.h"


class console_imgui : public console
{
private:
	std::thread  *th { nullptr };

protected:
	int wait_for_char_ll(const short timeout) override;

	void put_char_ll(const char c) override;

public:
	console_imgui(std::atomic_uint32_t *const stop_event);
	virtual ~console_imgui();

	void begin() override;

	void put_string_lf(const std::string & what) override;
	void resize_terminal() override;
	void refresh_virtual_terminal() override;

	void panel_update_thread() override;

	void operator()();
};
#endif
