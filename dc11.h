// (C) 2024 by Folkert van Heusden
// Released under MIT license

#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "comm.h"
#include "device.h"
#include "gen.h"
#include "bus.h"
#include "log.h"

#define DC11_RCSR 0174000 // receiver status register
#define DC11_BASE DC11_RCSR
#define DC11_END  (DC11_BASE + (4 * 4 + 1) * 2)  // 4 interfaces, + 2 to point behind it

class Stream;
class bus;

// 4 interfaces
constexpr const int dc11_n_lines = 4;

class dc11: public device
{
private:
	bus              *const b          { nullptr };
	uint16_t          registers[4 * dc11_n_lines] { 0 };
	std::atomic_bool  stop_flag        { false   };
	std::thread      *th               { nullptr };

	std::vector<comm *> comm_interfaces;
	std::vector<bool  > connected;

	std::vector<char>   recv_buffers[dc11_n_lines];
        mutable std::mutex  input_lock  [dc11_n_lines];

	void trigger_interrupt(const int line_nr, const bool is_tx);
	bool is_rx_interrupt_enabled(const int line_nr) const;
	bool is_tx_interrupt_enabled(const int line_nr) const;

public:
	dc11(bus *const b, const std::vector<comm *> & comm_interfaces);
	virtual ~dc11();

#if IS_POSIX
//	json_t *serialize();
//	static tty *deserialize(const json_t *const j, bus *const b, console *const cnsl);
#endif

	std::vector<comm *> *get_comm_interfaces() { return &comm_interfaces; }

	void reset() override;

	void show_state(console *const cnsl) const override;

	void test_port(const size_t port_nr, const std::string & txt) const;
	void test_ports(const std::string & txt) const;

	uint8_t  read_byte(const uint16_t addr) override;
	uint16_t read_word(const uint16_t addr) override;

	void write_byte(const uint16_t addr, const uint8_t  v) override;
	void write_word(const uint16_t addr, const uint16_t v) override;

	void operator()();
};
