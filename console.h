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

	char                    screen_buffer[t_height][t_width];
	uint8_t                 tx          { 0 };
	uint8_t                 ty          { 0 };

protected:
	std::atomic_bool *const terminate   { nullptr };

	bus              *const b           { nullptr };
	std::thread            *th          { nullptr };
	std::atomic_bool        disk_read_activity_flag  { false };
	std::atomic_bool        disk_write_activity_flag { false };
	std::atomic_bool        running_flag             { false };

	virtual int wait_for_char(const int timeout) = 0;

	virtual void put_char_ll(const char c) = 0;

	void put_string_ll(const std::string & what);

public:
	console(std::atomic_bool *const terminate, bus *const b);
	virtual ~console();

	bool    poll_char();

	uint8_t get_char();

	void    put_char(const char c);

	void    debug(const std::string fmt, ...);

	virtual void resize_terminal() = 0;

	void    operator()();

	std::atomic_bool * get_running_flag()             { return &running_flag; }
	std::atomic_bool * get_disk_read_activity_flag()  { return &disk_read_activity_flag; }
	std::atomic_bool * get_disk_write_activity_flag() { return &disk_write_activity_flag; }

	virtual void panel_update_thread() = 0;
};
