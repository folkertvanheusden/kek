// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <cassert>

#include "disk_backend.h"
#include "gen.h"
#if IS_POSIX
#include "disk_backend_file.h"
#include "disk_backend_nbd.h"
#endif


disk_backend::disk_backend()
{
}

disk_backend::~disk_backend()
{
}

#if IS_POSIX
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

json_t *disk_backend::serialize_overlay() const
{
	json_t *out = json_object();

	for(auto & id: overlay) {
		json_t *j_data = json_array();

		for(size_t i=0; i<id.second.size(); i++)
			json_array_append(j_data, json_integer(id.second.at(i)));

		json_object_set(out, format("%lu", id.first).c_str(), j_data);
	}

	return out;
}

void disk_backend::deserialize_overlay(const json_t *const j)
{
	json_t *input = json_object_get(j, "overlay");
	if (!input)  // we can have state-dumps without overlay
		return;

	const char *key   = nullptr;
	json_t     *value = nullptr;
	json_object_foreach(input, key, value) {
		uint32_t id = std::atoi(key);

		std::vector<uint8_t> data;
		for(size_t i=0; i<json_array_size(value); i++)
			data.push_back(json_integer_value(json_array_get(value, i)));

		store_object_in_overlay(id, data);
	}
}

disk_backend *disk_backend::deserialize(const json_t *const j)
{
	std::string   type = json_string_value(json_object_get(j, "disk-backend-type"));

	disk_backend *d    = nullptr;

	if (type == "file")
		d = disk_backend_file::deserialize(j);

	else if (type == "nbd")
		d = disk_backend_nbd::deserialize(j);

	// should not be reached
	assert(d);

	d->deserialize_overlay(j);

	// assume we want snapshots (again?)
	d->begin(true);

	return d;
}
#endif
