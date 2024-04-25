// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include "gen.h"


class disk_backend
{
public:
	disk_backend();
	virtual ~disk_backend();

#if IS_POSIX
	virtual json_t *serialize() const = 0;
	static disk_backend *deserialize(const json_t *const j);
#endif

	virtual bool begin() = 0;

	virtual bool read(const off_t offset, const size_t n, uint8_t *const target) = 0;

	virtual bool write(const off_t offset, const size_t n, const uint8_t *const from) = 0;
};
