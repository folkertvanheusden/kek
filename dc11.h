// (C) 2024 by Folkert van Heusden
// Released under MIT license

#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <ws2tcpip.h>
#include <winsock2.h>
#else
#define SOCKET int
#define INVALID_SOCKET -1
#endif

#include "gen.h"
#include "bus.h"
#include "log.h"

#define DC11_RCSR 0174000 // receiver status register
#define DC11_BASE DC11_RCSR
#define DC11_END  (DC11_BASE + (4 * 4 + 1) * 2)  // 4 interfaces, + 2 to point behind it

class bus;
struct pollfd;

// 4 interfaces
constexpr const int dc11_n_lines = 4;

class dc11
{
private:
	int              base_port        { 1100    };
	bus             *const b          { nullptr };
	uint16_t         registers[4 * dc11_n_lines] { 0 };
	std::atomic_bool stop_flag        { false   };
	std::thread     *th               { nullptr };

	// not statically allocated because of compiling problems on arduino
#if defined(_WIN32)
	WSAPOLLFD        *pfds            { nullptr };
#else
	pollfd           *pfds            { nullptr };
#endif
	std::vector<char> recv_buffers[dc11_n_lines];
        std::mutex        input_lock[dc11_n_lines];

	void trigger_interrupt(const int line_nr, const bool is_tx);
	bool is_rx_interrupt_enabled(const int line_nr);
	bool is_tx_interrupt_enabled(const int line_nr);

public:
	dc11(const int base_port, bus *const b);
	virtual ~dc11();

#if IS_POSIX
//	json_t *serialize();
//	static tty *deserialize(const json_t *const j, bus *const b, console *const cnsl);
#endif

	void reset();

	uint8_t  read_byte(const uint16_t addr);
	uint16_t read_word(const uint16_t addr);

	void write_byte(const uint16_t addr, const uint8_t v);
	void write_word(const uint16_t addr, const uint16_t v);

	void operator()();
};
