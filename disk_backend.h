#pragma once

#include <stdint.h>
#include <sys/types.h>


class disk_backend
{
public:
	disk_backend();
	virtual ~disk_backend();

	virtual bool begin() = 0;

	virtual bool read(const off_t offset, const size_t n, uint8_t *const target) = 0;

	virtual bool write(const off_t offset, const size_t n, const uint8_t *const from) = 0;
};
