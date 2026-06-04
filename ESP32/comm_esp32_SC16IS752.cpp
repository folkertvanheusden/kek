// (C) 2025-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32)
#include <SC16IS752.h>
#include <Wire.h>
#include <driver/uart.h>

#include "comm_esp32_SC16IS752.h"
#include "utils.h"


comm_esp32_SC16IS752::comm_esp32_SC16IS752(SC16IS752 *const parent, int dev_nr, const int port_nr):
	parent(parent),
	dev_nr(dev_nr),
	port_nr(port_nr)
{
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
	return format("SC16IS752:%d/%d", port_nr, baud_rate);
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

#if IS_POSIX
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

void comm_esp32_SC16IS752::configure_port(const int baud_rate)
{
	if (port_nr == 0)
		parent->beginA(baud_rate);
	else
		parent->beginB(baud_rate);
	this->baud_rate = baud_rate;
}

SC16IS752            *SC16IS752_a        { nullptr    };
SC16IS752            *SC16IS752_b        { nullptr    };
comm_esp32_SC16IS752 *SC16IS752_com_a[2] { nullptr    };
comm_esp32_SC16IS752 *SC16IS752_com_b[2] { nullptr    };

// scan for SC16IS752 devices
bool i2c_probe(comm *const cs, const byte addr)
{
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      cs->println(format("i2c device found at %02x", addr));
      return true;
    }
    return false;
}

void test_SC16IS752(comm *const cs, SC16IS752 *const p, const uint8_t which)
{
  cs->println(format("PING result for SC16IS752 @ 0x%02x: %d", which, p->ping()));
}

void search_SC16IS752(comm *const cs)
{
  cs->println("Scanning i2c bus for SC16IS752 devices...");
  Wire.begin();
  if (i2c_probe(cs, 0x4d)) {
    SC16IS752_a        = new SC16IS752(SC16IS750_PROTOCOL_I2C, 0x4d);
    SC16IS752_com_a[0] = new comm_esp32_SC16IS752(SC16IS752_a, 0, 0);
    SC16IS752_com_a[1] = new comm_esp32_SC16IS752(SC16IS752_a, 0, 1);
    test_SC16IS752(cs, SC16IS752_a, 0x4d);
  }
  if (i2c_probe(cs, 0x4e)) {
    SC16IS752_b = new SC16IS752(SC16IS750_PROTOCOL_I2C, 0x4e);
    SC16IS752_com_b[0] = new comm_esp32_SC16IS752(SC16IS752_a, 1, 0);
    SC16IS752_com_b[1] = new comm_esp32_SC16IS752(SC16IS752_a, 1, 1);
    test_SC16IS752(cs, SC16IS752_a, 0x4e);
  }

  cs->set_comm(SC16IS752_a, SC16IS752_b);
}
#endif
