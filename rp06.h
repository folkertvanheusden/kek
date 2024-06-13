// (C) 2024 by Folkert van Heusden
// Released under MIT license
// Some of the code is translated from Neil Webber's PDP11/70 emulator

#pragma once

#include "gen.h"
#include <ArduinoJson.h>
#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "disk_device.h"
#include "disk_backend.h"


#define RP06_CS1 	0176700  // control register
#define RP06_WC	 	0176702  // word count
#define RP06_UBA	0176704  // UNIBUS address
#define RP06_DA	 	0176706  // desired address
#define RP06_CS2	0176710  // control/status register 2
#define RP06_DS	 	0176712  // drive status
#define RP06_ERRREG1    0176714  // error register 1
#define RP06_AS	 	0176716  // unified attention status
#define RP06_RMLA	0176720  // lookahead (sector under head!!)
#define RP06_OFR	0176732  // heads offset
#define RP06_DC	 	0176734  // desired cylinder
#define RP06_CC	 	0176736  // "current cylinder" and/or holding register
#define RP06_BAE	0176750  // address extension (pdp11/70 extra phys bits)
#define RP06_BASE  RP06_CS1
#define RP06_END  (RP06_BAE + 2)


class bus;

class rp06: public disk_device
{
private:
	bus      *const b;

	uint16_t registers[32] { };

	std::atomic_bool *const disk_read_activity  { nullptr };
	std::atomic_bool *const disk_write_activity { nullptr };

	int      reg_num(uint16_t addr) const;
	uint32_t getphysaddr() const;
	uint32_t compute_offset() const;

public:
	rp06(bus *const b, std::atomic_bool *const disk_read_activity, std::atomic_bool *const disk_write_activity);
	virtual ~rp06();

	void begin() override;
	void reset() override;

	void show_state(console *const cnsl) const override;

	JsonDocument serialize() const;
	static rp06 *deserialize(const JsonVariantConst j, bus *const b);

	uint8_t  read_byte(const uint16_t addr) override;
	uint16_t read_word(const uint16_t addr) override;

	void write_byte(const uint16_t addr, const uint8_t  v) override;
	void write_word(const uint16_t addr, const uint16_t v) override;

	enum class ds_bits {
		OFM = 0000001,  // offset mode
		VV  = 0000100,  // volume valid
		DRY = 0000200,  // drive ready
		DPR = 0000400,  // drive present
		MOL = 0010000   // medium online
	};

	enum class cs1_bits {
		GO  = 0000001,  // GO bit
		FN  = 0000076,  // 5 bit function code - this is the mask
		IE  = 0000100,  // Interrupt enable
		RDY = 0000200,  // Drive ready
		A16 = 0000400,
		A17 = 0001000,
		TRE = 0040000,
	};
};
