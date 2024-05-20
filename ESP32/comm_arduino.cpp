// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32)
#include <driver/uart.h>

#include "comm_arduino.h"
#include "utils.h"


comm_arduino::comm_arduino(Stream *const s, const std::string & name): s(s), name(name)
{
}

comm_arduino::~comm_arduino()
{
}

bool comm_arduino::begin()
{
	return true;
}

std::string comm_arduino::get_identifier() const
{
	return name;
}

bool comm_arduino::is_connected()
{
	return true;
}

bool comm_arduino::has_data()
{
	return s->available();
}

uint8_t comm_arduino::get_byte()
{
	while(!has_data())
		vTaskDelay(5 / portTICK_PERIOD_MS);

	return s->read();
}

void comm_arduino::send_data(const uint8_t *const in, const size_t n)
{
	s->write(in, n);
}

JsonDocument comm_arduino::serialize() const
{
	JsonDocument j;

	j["comm-backend-type"] = "arduino";

	j["name"] = name;

	return j;
}

comm_arduino *comm_arduino::deserialize(const JsonVariantConst j)
{
	comm_arduino *r = new comm_arduino(&Serial, j["name"].as<std::string>());
	r->begin();  // TODO error-checking

	return r;
}
#endif
