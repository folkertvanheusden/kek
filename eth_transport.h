#pragma once

#include "gen.h"
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

class console;

class eth_transport
{
protected:
	uint64_t    pkt_cnt_rx     { 0 };
	uint64_t    pkt_cnt_tx     { 0 };

public:
	eth_transport();
	virtual ~eth_transport();

	virtual bool begin() = 0;

	virtual std::string identifier() const = 0;
	virtual void show_state(console *const cnsl) const;

	virtual bool transmit(const uint8_t *const data, const size_t n_bytes) = 0;
	virtual std::pair<uint8_t *, size_t> get(const int timeout) = 0;
};
