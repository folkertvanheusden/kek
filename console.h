#pragma once

#include <atomic>
#include <optional>
#include <thread>
#include <vector>


class console
{
private:
	std::atomic_bool *const terminate   { nullptr };

	std::thread            *th          { nullptr };

	std::vector<char>       buffer;

protected:
	virtual std::optional<char> wait_for_char(const int timeout) = 0;

public:
	console(std::atomic_bool *const terminate);
	virtual ~console();

	bool    poll_char();

	uint8_t get_char();

	virtual void put_char(const char c) = 0;

	virtual void resize_terminal() = 0;

	void    operator()();
};
