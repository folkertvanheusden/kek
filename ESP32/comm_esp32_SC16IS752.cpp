// (C) 2025 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32)
#include <driver/uart.h>

#include "comm_esp32_SC16IS752.h"
#include "utils.h"


comm_esp32_SC16IS752::comm_esp32_SC16IS752(SC16IS752 *const parent, int dev_nr, const int port_nr):
	parent(parent),
	dev_nr(dev_nr),
	port_nr(port_nr)
{
	printf("comm_esp32_SC16IS752\r\n");
}

comm_esp32_SC16IS752::~comm_esp32_SC16IS752()
{
}

bool comm_esp32_SC16IS752::begin()
{
	initialized = true;

	return true;
}

std::string comm_esp32_SC16IS752::get_identifier() const
{
	return format("SC16IS752:%d", port_nr);
}

bool comm_esp32_SC16IS752::is_connected()
{
	return true;
}

bool comm_esp32_SC16IS752::has_data()
{
	return parent->available(port_nr);
}

uint8_t comm_esp32_SC16IS752::get_byte()
{
	while(!has_data())
		vTaskDelay(5 / portTICK_PERIOD_MS);

	return parent->read(port_nr);
}

void comm_esp32_SC16IS752::send_data(const uint8_t *const in, const size_t n)
{
	for(size_t i=0; i<n; i++)
		parent->write(port_nr, in[i]);
}

JsonDocument comm_esp32_SC16IS752::serialize() const
{
	JsonDocument j;
	j["dev-nr" ] = dev_nr;
	j["port-nr"] = port_nr;
	return j;
}

comm_esp32_SC16IS752 *comm_esp32_SC16IS752::deserialize(const JsonVariantConst j, SC16IS752 *const a, SC16IS752 *const b)
{
	int dev_nr  = j["dev-nr" ].as<int>();
	int port_nr = j["port-nr"].as<int>();
	comm_esp32_SC16IS752 *r = new comm_esp32_SC16IS752(dev_nr == 0 ? a: b, dev_nr, port_nr);
	r->begin();
	return r;
}
#endif
