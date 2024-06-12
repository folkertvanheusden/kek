// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <atomic>
#include <condition_variable>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "utils.h"

#if defined(_WIN32)
#include "win32.h"
#endif


class bus;

class console
{
private:
	std::vector<char>       input_buffer;
#if defined(BUILD_FOR_RP2040)
	QueueHandle_t           have_data  { xQueueCreate(16, 1)      };
	SemaphoreHandle_t       input_lock { xSemaphoreCreateBinary() };
#else
	std::condition_variable have_data;
	std::mutex              input_lock;
#endif

protected:
	std::atomic_uint32_t *const stop_event   { nullptr };
	std::atomic_bool        stop_panel       { false   };

	bus                    *b                { nullptr };
#if !defined(BUILD_FOR_RP2040)
	std::thread            *th               { nullptr };
#endif
	std::atomic_bool        disk_read_activity_flag  { false };
	std::atomic_bool        disk_write_activity_flag { false };
	std::atomic_bool        running_flag     { false };

	bool                    stop_thread_flag { false };

	const int               t_width          { 0 };
	const int               t_height         { 0 };
	char                   *screen_buffer    { nullptr };
	uint8_t                 tx               { 0 };
	uint8_t                 ty               { 0 };
	std::atomic_bool        timestamps       { false };
	const uint64_t          start_ts         { get_us() };

	const size_t            n_edit_lines_hist { 8 };  // maximum number of previous edit-lines
	std::vector<std::string> edit_lines_hist;

	std::string             debug_buffer;

	virtual int  wait_for_char_ll(const short timeout) = 0;

	virtual void put_char_ll(const char c) = 0;

public:
	console(std::atomic_uint32_t *const stop_event, const int t_width = 80, const int t_height = 25);
	virtual ~console();

	virtual void begin();

	void         set_bus(bus *const b) { this->b = b; }

	void         start_thread();
	void         stop_thread();

	bool         poll_char();
	int          get_char();
	std::optional<char> wait_char(const int timeout_ms);
	std::string  read_line(const std::string & prompt);
	void         flush_input();

	void         enable_timestamp(const bool state) { timestamps = state; }

	void         emit_backspace();
	void         put_char(const char c);
	void         put_string(const std::string & what);
	virtual void put_string_lf(const std::string & what) = 0;

	virtual void resize_terminal() = 0;

	virtual void refresh_virtual_terminal() = 0;

	void         operator()();

	std::atomic_bool * get_running_flag()             { return &running_flag; }
	std::atomic_bool * get_disk_read_activity_flag()  { return &disk_read_activity_flag; }
	std::atomic_bool * get_disk_write_activity_flag() { return &disk_write_activity_flag; }

	void         stop_panel_thread() { stop_panel = true; }
	virtual void panel_update_thread() = 0;
};
