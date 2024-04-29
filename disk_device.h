#pragma once

#include <vector>

#include "device.h"
#include "disk_backend.h"


class disk_device: public device
{
protected:
	std::vector<disk_backend *> fhs;

public:
	disk_device() {
	}

	virtual ~disk_device() {
	}

	virtual void begin() = 0;

	std::vector<disk_backend *> * access_disk_backends() { return &fhs; }
};
