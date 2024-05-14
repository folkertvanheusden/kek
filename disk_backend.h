// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <ArduinoJson.h>
#include <map>
#include <optional>
#include <stdint.h>
#include <string>
#include <vector>
#include <sys/types.h>

#include "gen.h"


class disk_backend
{
protected:
	bool use_overlay { false };
	std::map<off_t, std::vector<uint8_t> > overlay;

	void store_object_in_overlay(const off_t id, const std::vector<uint8_t> & data);
	bool store_mem_range_in_overlay(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size);
	std::optional<std::vector<uint8_t> > get_object_from_overlay(const off_t id);
	std::optional<std::vector<uint8_t> > get_from_overlay(const off_t offset, const size_t sector_size);

	JsonVariant serialize_overlay() const;
	void deserialize_overlay(const JsonVariant j);

public:
	disk_backend();
	virtual ~disk_backend();

	virtual JsonVariant serialize() const = 0;
	static disk_backend *deserialize(const JsonVariant j);

	virtual std::string get_identifier() const = 0;

	virtual bool begin(const bool disk_snapshots) = 0;

	virtual bool read(const off_t offset, const size_t n, uint8_t *const target, const size_t sector_size) = 0;

	virtual bool write(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size) = 0;
};
