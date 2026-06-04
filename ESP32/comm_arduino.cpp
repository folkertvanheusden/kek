// (C) 2024-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
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
	my_unique_lock lck(&lock);
	return s->available();
}

uint8_t comm_arduino::get_byte()
{
	my_unique_lock lck(&lock);
	return s->read();
}

void comm_arduino::send_data(const uint8_t *const in, const size_t n)
{
	my_unique_lock lck(&lock);
	s->write(in, n);
}

#if IS_POSIX
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
#endif
