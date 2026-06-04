// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include <cassert>

#include "disk_backend.h"
#include "gen.h"
#include "utils.h"
#if IS_POSIX || defined(_WIN32)
#include "disk_backend_file.h"
#endif
#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
#include "disk_backend_esp32.h"
#endif
#include "disk_backend_nbd.h"


disk_backend::disk_backend()
{
}

disk_backend::~disk_backend()
{
}

void disk_backend::store_object_in_overlay(const off_t id, const std::vector<uint8_t> & data)
{
	overlay.insert_or_assign(id, data);
}

std::optional<std::vector<uint8_t> > disk_backend::get_object_from_overlay(const off_t id)
{
	auto it = overlay.find(id);
	if (it != overlay.end())
		return it->second;

	return { };
}

std::optional<std::vector<uint8_t> > disk_backend::get_from_overlay(const off_t offset, const size_t sector_size)
{
	assert((offset % sector_size) == 0);

	if (use_overlay)
		return get_object_from_overlay(offset / sector_size);

	return { };
}

bool disk_backend::store_mem_range_in_overlay(const off_t offset, const size_t n, const uint8_t *const from, const size_t sector_size)
{
	assert((offset % sector_size) == 0);
	assert((n % sector_size) == 0);

	if (use_overlay) {
		for(size_t o=0; o<n; o += sector_size)
			store_object_in_overlay((offset + o) / sector_size, std::vector<uint8_t>(from + o, from + o + sector_size));

		return true;
	}

	return false;
}

#if IS_POSIX
JsonDocument disk_backend::serialize_overlay() const
{
	JsonDocument out;

	for(auto & id: overlay) {
		JsonDocument j_data;
		JsonArray j_data_work = j_data.to<JsonArray>();

		for(size_t i=0; i<id.second.size(); i++)
			j_data_work.add(id.second.at(i));

		out[format("%" PRIlu "", id.first)] = j_data;
	}

	return out;
}

void disk_backend::deserialize_overlay(const JsonVariantConst j)
{
	if (j.containsKey("overlay") == false)
		return; // we can have state-dumps without overlay

	for(auto kv : j.as<JsonObjectConst>()) {
		uint32_t id = std::atoi(kv.key().c_str());

		std::vector<uint8_t> data;
		for(auto v: kv.value().as<JsonArrayConst>())
			data.push_back(v);

		store_object_in_overlay(id, data);
	}
}

disk_backend *disk_backend::deserialize(const JsonVariantConst j)
{
	std::string   type = j["disk-backend-type"];

	disk_backend *d    = nullptr;

	if (type == "nbd")
		d = disk_backend_nbd::deserialize(j);
#if IS_POSIX || defined(_WIN32)
	else if (type == "file")
		d = disk_backend_file::deserialize(j);
#else
	else if (type == "esp32")
		d = disk_backend_esp32::deserialize(j);
#endif

	// should not be triggered
	assert(d);

	d->deserialize_overlay(j);

	// assume we want snapshots (again?)
	d->begin(true);

	return d;
}

constexpr const uint32_t CRC32_POLY = 0xEDB88320;  // reverse of 0x04C11DB7
static uint32_t crc32(uint32_t crc, uint8_t data)
{
	crc ^= data;
	for(int i = 8; i > 0; i--) {
		if (crc & 1)
			crc = (crc >> 1) ^ CRC32_POLY;
		else
			crc >>= 1;
	}
	return crc;
}

std::optional<uint32_t> disk_backend::crc_over_data()
{
	uint8_t  buffer[4096];
	uint32_t crc = ~0;
	for(uint64_t offset=0; offset<size; offset += sizeof buffer) {
		if (!read(offset, 1, buffer, sizeof buffer))
			return { };

		for(size_t i=0; i<sizeof buffer; i++)
			crc = crc32(crc, buffer[i]);
	}
	return crc;
}
#endif
