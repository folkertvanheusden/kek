// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <cstring>

#include "comm.h"


comm::comm()
{
}

comm::~comm()
{
}

void comm::println(const char *const s)
{
	send_data(reinterpret_cast<const uint8_t *>(s), strlen(s));
	send_data(reinterpret_cast<const uint8_t *>("\r\n"), 2);
}

void comm::println(const std::string & in)
{
	send_data(reinterpret_cast<const uint8_t *>(in.c_str()), in.size());
	send_data(reinterpret_cast<const uint8_t *>("\r\n"), 2);
}
