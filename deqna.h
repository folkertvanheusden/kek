// (C) 2026 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <thread>
#include <vector>

#include "bus.h"
#include "device.h"
#include "eth_transport.h"
#include "my_lock.h"

#define DEQNA_BASE    0174440
#define DEQNA_RX_BDLL 0174444
#define DEQNA_RX_BDLH 0174446
#define DEQNA_TX_BDLL 0174450
#define DEQNA_TX_BDLH 0174452
#define DEQNA_VECTOR  0174454
#define DEQNA_CSR     0174456
#define DEQNA_END    (DEQNA_CSR + 2)
#define DEQNA_IRQ_LEVEL    4
#define DEQNA_MAX_N_QUEUED 1

FLASHMEM class deqna : public device
{
public:
	enum monitor_mode_t { nothing, filtered, everything };

private:
	bus             *const b        { nullptr };
	eth_transport   *const eth_dev  { nullptr };
	std::atomic_uint16_t registers[8] { 0     };  // accessed from multiple threads
	uint8_t          mac_address[6] { 0       };
	int              dev_fd         { -1      };
	abool            stop_flag      { false   };
	monitor_mode_t   monitor_mode   { nothing };
	console         *cnsl           { nullptr };
	abool           *activity_flag  { nullptr };
#if defined(FREERTOS)
	abool rx_low_stopped            { false   };
	abool rx_high_stopped           { false   };
#else
	std::thread     *th_rx_low      { nullptr };
	std::thread     *th_rx_high     { nullptr };
#endif
	mutable my_lock  lock;
	my_threadsafe_queue<std::pair<uint8_t *, size_t> > received;
	big_acounter total_n_rx_pkts { 0 };
	big_acounter total_n_rx_drop { 0 };
	big_acounter total_n_tx_pkts { 0 };
	big_acounter total_n_tx_drop { 0 };
	big_acounter total_n_tx_fail { 0 };

	void queue_rx_packet(const uint8_t *const in, const size_t n);
	void transmitter    ();
	void purge_buffers  ();

public:
	deqna(bus *const b, const uint8_t mac_address[6], eth_transport *const eth_dev, abool *const activity_flag);
	virtual ~deqna();

	bool begin();

	// need to be public for the thread wrappers
	void receiver_low   ();
	void receiver_high  ();

	void reset(const bool hard) override;

	void show_state(console *const cnsl) const override;
	bool test(console *const cnsl);
	void set_monitor_mode(const monitor_mode_t mode, console *const cnsl) { monitor_mode = mode; this->cnsl = cnsl; }

	uint8_t  read_byte(const uint16_t addr) override;
	uint16_t read_word(const uint16_t addr) override;

	void write_byte(const uint16_t addr, const uint8_t  v) override;
	void write_word(const uint16_t addr, const uint16_t v) override;
};

void get_deqna_mac(uint8_t *const to);
