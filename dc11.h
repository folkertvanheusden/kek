// (C) 2024 by Folkert van Heusden
// Released under MIT license

#pragma once
#include <atomic>
#include <cstdint>

#include "gen.h"
#include "bus.h"
#include "log.h"

#define DC11_RCSR 0174000 // receiver status register
#define DC11_BASE DC11_RCSR
#define DC11_END  (DC11_BASE + 4 * 4 + 2)  // 4 interfaces, + 2 to point after it

class bus;

class dc11
{
private:
	bus             *const b          { nullptr };
	// 4 interfaces
	uint16_t         registers[4 * 4] {         };
	std::atomic_bool stop_flag        { false   };

public:
	dc11(bus *const b);
	virtual ~dc11();

#if IS_POSIX
//	json_t *serialize();
//	static tty *deserialize(const json_t *const j, bus *const b, console *const cnsl);
#endif

	void reset();

	uint8_t read_byte(const uint16_t addr);
	uint16_t read_word(const uint16_t addr);

	void write_byte(const uint16_t addr, const uint8_t v);
	void write_word(const uint16_t addr, uint16_t v);
};
