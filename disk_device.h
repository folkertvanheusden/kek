#pragma once

#include <vector>

#include "console.h"
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
	void show_disk_backends(console *cnsl) const {
		cnsl->put_string_lf("Disk backend(s):");
		for(auto d: fhs)
			d->show_state(cnsl);
	}
};
