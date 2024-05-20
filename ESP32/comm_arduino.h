// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <Arduino.h>
#include "comm.h"


class comm_arduino: public comm
{
private:
	Stream *const s;
	std::string   name;

public:
	comm_arduino(Stream *const s, const std::string & name);
	virtual ~comm_arduino();

	bool    begin() override;

	JsonDocument serialize() const override;
	static comm_arduino *deserialize(const JsonVariantConst j);

	std::string get_identifier() const;

	bool    is_connected() override;

	bool    has_data() override;
	uint8_t get_byte() override;

	void    send_data(const uint8_t *const in, const size_t n) override;
};
