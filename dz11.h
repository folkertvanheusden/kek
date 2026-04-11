// (C) 2026 by Folkert van Heusden
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

class bus;

// 8 interfaces
constexpr const int dz11_n_lines = 8;
constexpr const int n_dz11_registers = 7;

#define DZ11_INTERRUPT_VECTOR_RX 0310
#define DZ11_INTERRUPT_VECTOR_TX 0314
#define DZ11_BASE 0160100
#define DZ11_CSR   DZ11_BASE
#define DZ11_RBUF (DZ11_BASE + 1 * 2)
#define DZ11_LPR  (DZ11_BASE + 1 * 2)
#define DZ11_TCR  (DZ11_BASE + 2 * 2)
#define DZ11_MSR  (DZ11_BASE + 3 * 2)
#define DZ11_TDR  (DZ11_BASE + 3 * 2)
#define DZ11_END  (DZ11_BASE + 4 * 2)  // 4 register slots

class dz11: public device
{
private:
	bus              *const b      { nullptr };
	uint16_t          registers[n_dz11_registers] { 0 };
	std::atomic_bool  stop_flag    { false   };
	std::thread      *th           { nullptr };
	bool              flipflop_txd { false   };
	size_t            scanner_line_nr { 0    };

	std::vector<comm *> comm_interfaces;
	std::vector<bool  > connected;

	std::vector<char>   recv_buffers[dz11_n_lines];
        mutable std::mutex  input_lock;

	void trigger_interrupt(const bool is_tx);
	bool is_rx_interrupt_enabled() const;
	bool is_tx_interrupt_enabled() const;
	void tx_scanner_do(const int line, const bool force = false);
	void tx_scanner(const std::optional<int> line, const bool force = false);

public:
	dz11(bus *const b, const std::vector<comm *> & comm_interfaces);
	virtual ~dz11();

	bool begin();

	JsonDocument serialize() const;
	static dz11 *deserialize(const JsonVariantConst j, bus *const b);

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
