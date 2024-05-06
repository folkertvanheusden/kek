// (C) 2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>


class ntp
{
private:
	std::atomic_bool stop         { false   };
	std::mutex       lock;
	std::string      server;
	std::thread     *th           { nullptr };
	uint32_t         millis_at_ts { 0       };
	uint64_t         ntp_at_ts    { 0       };  // milliseconds!

public:
	ntp(const std::string & upstream_server);
	virtual ~ntp();

	void begin();

	std::optional<uint64_t> get_unix_epoch_ms();

	void operator()();
};
