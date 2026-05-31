// (C) 2026 by Folkert van Heusden
// Released under MIT license

#if defined(USE_IMGUI)
#include <atomic>
#include <thread>
#include <SDL3/SDL_render.h>

#include "console.h"
#include "my_lock.h"


class console_imgui : public console
{
private:
	std::atomic_bool             stop  { false   };
	std::thread                 *th    { nullptr };
	my_threadsafe_queue<uint8_t> kb_buffer;
	SDL_Texture                 *panel { nullptr };
	my_lock                      panel_lock;

protected:
	int  wait_for_char_ll(const int  timeout) override;
	void put_char_ll     (const char c      ) override;

public:
	console_imgui(std::atomic_uint32_t *const stop_event);
	virtual ~console_imgui();

	void begin() override;

	void put_string_lf(const std::string & what) override;
	void resize_terminal() override;
	void refresh_virtual_terminal() override;

	void panel_update_thread() override;

	void gui_event_loop();
};
#endif
