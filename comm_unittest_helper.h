#pragma once

#include "gen.h"
#include "ArduinoJson.h"
#include "comm.h"


class bus;

class comm_unittest_helper: public comm
{
private:
	bool                 connected { false };
	std::vector<uint8_t> data_rx;
	std::vector<uint8_t> data_tx;

public:
	comm_unittest_helper() {
	}

	virtual ~comm_unittest_helper() {
	}

	virtual bool    begin() override { return true; }

	virtual JsonDocument serialize() const override { 
	}

	virtual std::string get_identifier() const override { return "unittest helper"; }

	void            set_connected(const bool state) { connected = state; }
	virtual bool    is_connected() override { return connected; }

	void            set_data(const std::vector<uint8_t> & data_in) { data_rx = data_in; }
	virtual bool    has_data() override { return data_rx.empty() == false; }
	virtual uint8_t get_byte() override { auto rc = data_rx.front(); data_rx.erase(data_rx.begin()); return rc; }

	virtual void    send_data(const uint8_t *const in, const size_t n) override { data_tx = std::vector<uint8_t>(in, in + n); }
	std::vector<uint8_t> get_tx_data() { auto rc = data_tx; data_tx.clear(); return rc; }
};
