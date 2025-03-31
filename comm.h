// (C) 2024-2025 by Folkert van Heusden
// Released under MIT license

#pragma once

#include "gen.h"
#include <cstddef>
#include <cstdint>
#include <string>

#include "ArduinoJson.h"

#if defined(ESP32)
#include <SC16IS752.h>
#endif


class bus;

class comm
{
private:
#if defined(ESP32)
	static SC16IS752 *dc11_inst_1;
	static SC16IS752 *dc11_inst_2;
#endif
public:
	comm();
	virtual ~comm();

	virtual bool    begin() = 0;

#if defined(ESP32)
	void            set_comm(SC16IS752 *const a, SC16IS752 *const b);
#endif
	virtual JsonDocument serialize() const = 0;
	static comm    *deserialize(const JsonVariantConst j, bus *const b);

	virtual std::string get_identifier() const = 0;

	virtual bool    is_connected() = 0;

	virtual bool    has_data() = 0;
	virtual uint8_t get_byte() = 0;

	virtual void    send_data(const uint8_t *const in, const size_t n) = 0;

        void            println(const char *const s);
        void            println(const std::string & in);
};
