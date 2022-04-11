#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include "bus.h"


constexpr const int t_width  { 80 };
constexpr const int t_height { 25 };

class console
{
private:
	std::vector<char>       input_buffer;

protected:
	std::atomic_bool *const terminate   { nullptr };

	bus              *const b           { nullptr };
	std::thread            *th          { nullptr };
	std::atomic_bool        disk_read_activity_flag  { false };
	std::atomic_bool        disk_write_activity_flag { false };
	std::atomic_bool        running_flag             { false };

	char                    screen_buffer[t_height][t_width];
	uint8_t                 tx          { 0 };
	uint8_t                 ty          { 0 };

	virtual void put_char_ll(const char c) = 0;

public:
	console(std::atomic_bool *const terminate, bus *const b);
	virtual ~console();

	virtual void start_thread() = 0;

	virtual int  wait_for_char(const short timeout) = 0;

	bool         poll_char();
	uint8_t      get_char();
	std::string  read_line(const std::string & prompt);
	void         flush_input();

	void         put_char(const char c);
	void         put_string(const std::string & what);
	virtual void put_string_lf(const std::string & what);

	void         debug(const std::string fmt, ...);

	virtual void resize_terminal() = 0;

	virtual void refresh_virtual_terminal() = 0;

	void         operator()();

	std::atomic_bool * get_running_flag()             { return &running_flag; }
	std::atomic_bool * get_disk_read_activity_flag()  { return &disk_read_activity_flag; }
	std::atomic_bool * get_disk_write_activity_flag() { return &disk_write_activity_flag; }

	virtual void panel_update_thread() = 0;
};
