// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#pragma once

#include "gen.h"
#if IS_POSIX
#include <ArduinoJson.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "disk_device.h"
#include "disk_backend.h"


#define RK05_DS		0177400	// drive status
#define RK05_ERROR	0177402 // error
#define RK05_CS		0177404 // control status
#define RK05_WC		0177406 // word count
#define RK05_BA		0177410 // bus address
#define RK05_DA		0177412 // disk address
#define RK05_DATABUF	0177414 // data buffer
#define RK05_BASE	RK05_DS
#define RK05_END	(RK05_DATABUF + 2)

class bus;

class rk05: public disk_device
{
private:
	bus      *const b                { nullptr };
	uint16_t        registers  [7]   { 0       };
	uint8_t         xfer_buffer[512] { 0       };

	abool *const disk_read_activity  { nullptr };
	abool *const disk_write_activity { nullptr };

	uint32_t get_bus_address() const;
	void     update_bus_address(const uint16_t v);

public:
	rk05(bus *const b, abool *const disk_read_acitivity, abool *const disk_write_acitivity);
	virtual ~rk05();

	void begin() override;
	void reset(const bool hard) override;

	void show_state(console *const cnsl) const override;

#if IS_POSIX
	JsonDocument serialize() const;
	static rk05 *deserialize(const JsonVariantConst j, bus *const b);
#endif

	uint8_t  read_byte(const uint16_t addr) override;
	uint16_t read_word(const uint16_t addr) override;

	void write_byte(const uint16_t addr, const uint8_t  v) override;
	void write_word(const uint16_t addr, const uint16_t v) override;
};
