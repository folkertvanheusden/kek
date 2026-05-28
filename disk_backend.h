// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#pragma once

#include "gen.h"
#include <ArduinoJson.h>
#include <map>
#include <optional>
#include <stdint.h>
#include <string>
#include <vector>
#include <sys/types.h>

#include "console.h"


class disk_backend
{
protected:
	uint64_t size        { 0     };
	bool     use_overlay { false };
	std::map<off_t, std::vector<uint8_t> > overlay;

	void store_object_in_overlay(const off_t id, const std::vector<uint8_t> & data);
	bool store_mem_range_in_overlay(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size);
	std::optional<std::vector<uint8_t> > get_object_from_overlay(const off_t id);
	std::optional<std::vector<uint8_t> > get_from_overlay(const off_t offset, const size_t sector_size);

	JsonDocument serialize_overlay() const;
	void         deserialize_overlay(const JsonVariantConst j);
	std::optional<uint32_t> crc_over_data();

public:
	disk_backend();
	virtual ~disk_backend();

	virtual JsonDocument serialize() = 0;
	static disk_backend *deserialize(const JsonVariantConst j);

	virtual std::string get_identifier() const = 0;
	virtual void show_state(console *const cnsl) const = 0;

	virtual bool begin(const bool disk_snapshots) = 0;

	virtual bool read(const off_t offset, const size_t n, uint8_t *const target, const size_t sector_size) = 0;

	virtual bool write(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size) = 0;
};
