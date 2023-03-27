// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <atomic>
#include <condition_variable>
#include <thread>
#include <vector>

#include "bus.h"

#if defined(_WIN32)
#include "win32.h"
#endif


constexpr const int t_width  { 80 };
constexpr const int t_height { 25 };

class console
{
private:
	std::vector<char>       input_buffer;
	std::condition_variable have_data;
	std::mutex              input_lock;

protected:
	std::atomic_uint32_t *const stop_event    { nullptr };

	bus              *const b                { nullptr };
	std::thread            *th               { nullptr };
	std::atomic_bool        disk_read_activity_flag  { false };
	std::atomic_bool        disk_write_activity_flag { false };
	std::atomic_bool        running_flag     { false };

	bool                    stop_thread_flag { false };

	char                    screen_buffer[t_height][t_width];
	uint8_t                 tx               { 0 };
	uint8_t                 ty               { 0 };

	std::string             debug_buffer;

	virtual int  wait_for_char_ll(const short timeout) = 0;

	virtual void put_char_ll(const char c) = 0;

public:
	console(std::atomic_uint32_t *const stop_event, bus *const b);
	virtual ~console();

	void         start_thread();
	void         stop_thread();

	bool         poll_char();
	int          get_char();
	int          wait_char(const int timeout_ms);
	std::string  read_line(const std::string & prompt);
	void         flush_input();

	void         emit_backspace();
	void         put_char(const char c);
	void         put_string(const std::string & what);
	virtual void put_string_lf(const std::string & what) = 0;

	void         debug(const std::string fmt, ...);

	virtual void resize_terminal() = 0;

	virtual void refresh_virtual_terminal() = 0;

	void         operator()();

	std::atomic_bool * get_running_flag()             { return &running_flag; }
	std::atomic_bool * get_disk_read_activity_flag()  { return &disk_read_activity_flag; }
	std::atomic_bool * get_disk_write_activity_flag() { return &disk_write_activity_flag; }

	virtual void panel_update_thread() = 0;
};
