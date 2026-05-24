// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#pragma once

#include "gen.h"
#include <functional>
#include <optional>
#include <string>
#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
#include <thread>
#endif
#include <vector>

#include "my_lock.h"
#include "utils.h"

#if defined(_WIN32)
#include "win32.h"
#endif


class blinkenlights;
class bus;
class tty;

class console
{
private:
	my_threadsafe_queue<char> input_buffer;
	my_lock                 put_string_lock;

protected:
	kek_event_t            *const stop_event { nullptr };
	abool                   stop_panel       { false   };
	blinkenlights          *p_blinkenlights  { nullptr };

	bus                    *b                { nullptr };
#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
	std::thread            *th_kb            { nullptr };
	std::thread            *th_panel         { nullptr };
#endif
	int                     refreshrate      { 15      };
	abool                   disk_read_activity_flag  { false };
	abool                   disk_write_activity_flag { false };
	abool                   network_activity_flag    { false };
	abool                   running_flag     { false };

	bool                    stop_thread_flag { false };

	const int               t_width          { 0 };
	const int               t_height         { 0 };
	char                   *screen_buffer    { nullptr };
	uint8_t                 tx               { 0 };
	uint8_t                 ty               { 0 };
	abool                   timestamps       { false };
	const uint64_t          start_ts         { get_us() };

	const size_t            n_edit_lines_hist { 8 };  // maximum number of previous edit-lines
	std::vector<std::string> edit_lines_hist;

	std::string             debug_buffer;

	tty                    *have_data_cb_notifier { nullptr };

	virtual int  wait_for_char_ll(const short timeout) = 0;

	virtual void put_char_ll(const char c) = 0;

public:
	console(kek_event_t *const stop_event, const int t_width = 80, const int t_height = 25);
	virtual ~console();

	virtual void begin();

	void         set_bus(bus *const b) { this->b = b; }

	void         start_thread();
	void         stop_thread();

	bool         poll_char();
	int          get_char();
	void         unget_char(const char c);
	std::optional<int> wait_char(const int timeout_ms);
	std::string  read_line(const std::string & prompt);
	void         flush_input();
	void         set_data_cb_notifier(auto notifier) { have_data_cb_notifier = notifier; }

	void         enable_timestamp(const bool state) { timestamps = state; }

	void         emit_backspace();
	void         put_char(const char c);
	void         put_string(const std::string & what);
	virtual void put_string_lf(const std::string & what) = 0;

	virtual void resize_terminal() = 0;

	virtual void refresh_virtual_terminal() = 0;

	virtual void operator()();

	abool * get_running_flag()             { return &running_flag;             }
	abool * get_disk_read_activity_flag()  { return &disk_read_activity_flag;  }
	abool * get_disk_write_activity_flag() { return &disk_write_activity_flag; }
	abool * get_network_activity_flag()    { return &network_activity_flag;    }

	void         set_blinkenlights_panel(blinkenlights *const p_blinkenlights);
	void         stop_panel_thread() { stop_panel = true; }
	virtual void panel_update_thread() = 0;
	int          get_refreshrate(              ) const { return refreshrate; }
	void         set_refreshrate(const int rate)       { refreshrate = rate; }

	virtual void set_LED_state   (const bool state);
	virtual void toggle_LED_state();
};
