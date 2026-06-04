// (C) 2026-2026 by Folkert van Heusden
// Released under MIT license

#pragma once

#include "gen.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if IS_POSIX
#include "ArduinoJson.h"
#endif
#include "my_lock.h"

#if defined(ESP32)
#include <SC16IS752.h>
#endif


class bus;

class comm
{
private:
#if defined(ESP32)
	static SC16IS752 *ser2_inst_1;
	static SC16IS752 *ser2_inst_2;
#endif
public:
	comm();
	virtual ~comm();

	virtual bool    begin() = 0;
	virtual bool    need_dealloc() { return true; }

#if defined(ESP32)
	void            set_comm(SC16IS752 *const a, SC16IS752 *const b);
#endif
#if IS_POSIX
	virtual JsonDocument serialize() const = 0;
	static comm    *deserialize(const JsonVariantConst j, bus *const b);
#endif

	virtual std::string get_identifier() const = 0;

	virtual bool    is_connected() = 0;

	virtual bool    has_data() = 0;
	virtual uint8_t get_byte() = 0;

	virtual void    send_data(const uint8_t *const in, const size_t n) = 0;

        void            println(const char *const s);
        void            println(const std::string & in);
};

struct comm_io
{
	mutable my_lock     lock;
	std::vector<comm *> channels;

	comm_io(const int max_n) {
		channels.resize(max_n);
	}

	comm_io(const comm_io & input) {
		channels = std::move(input.channels);
	}

	~comm_io() {
		for(auto & c: channels)
			delete c;
	}

#if IS_POSIX
	JsonDocument serialize() const {
		my_unique_lock lck(&lock);

		JsonDocument j_empty;
		j_empty["comm-backend-type"] = "none";

		JsonDocument j_interfaces;
		JsonArray    j_interfaces_work = j_interfaces.to<JsonArray>();
		for(auto & c: channels)
			j_interfaces_work.add(c ? c->serialize() : j_empty);

		return j_interfaces;
	}

	static comm_io *deserialize(const JsonVariantConst j, bus *const b) {
		std::vector<comm *> temp;
		JsonArrayConst j_interfaces = j;
		for(auto v: j_interfaces)
			temp.push_back(comm::deserialize(v, b));
		comm_io *out = new comm_io(temp.size());
		out->channels = temp;
		return out;
	}
#endif

	bool set_device(const int idx, comm *const p) {
		my_unique_lock lck(&lock);
		if (channels[idx] && channels[idx]->need_dealloc() == true)
			delete channels[idx];
		channels[idx] = p;
		return p->begin();
	}

	bool is_defined(const int idx) {
		my_unique_lock lck(&lock);
		return channels[idx] != nullptr;
	}

	void unload_device(const int idx) {
		my_unique_lock lck(&lock);
		delete channels[idx];
		channels[idx] = nullptr;
	}

	std::string get_identifier(const int idx) {
		my_unique_lock lck(&lock);
		if (channels[idx])
			return channels[idx]->get_identifier();
		return "NOT CONNECTED";
	}

	bool is_connected(const int idx) {
		my_unique_lock lck(&lock);
		if (channels[idx])
			return channels[idx]->is_connected();
		return false;
	}

	bool has_data(const int idx) {
		my_unique_lock lck(&lock);
		if (channels[idx])
			return channels[idx]->has_data();
		return false;
	}

	uint8_t get_byte(const int idx) {
		my_unique_lock lck(&lock);
		if (channels[idx])
			return channels[idx]->get_byte();
		return 0xee;
	}

	void send_data(const int idx, const uint8_t *const in, const size_t n) const {
		my_unique_lock lck(&lock);
		if (channels[idx])
			channels[idx]->send_data(in, n);
	}

        void println(const int idx, const char *const s) const {
		my_unique_lock lck(&lock);
		if (channels[idx])
			channels[idx]->println(s);
	}

        void println(const int idx, const std::string & in) const {
		my_unique_lock lck(&lock);
		if (channels[idx])
			channels[idx]->println(in);
	}
};
