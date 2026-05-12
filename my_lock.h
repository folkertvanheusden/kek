#pragma once

#include "gen.h"

#if defined(RP2040)
#include <semphr.h>
#else
#include <mutex>
#endif


class my_lock
{
private:
#if defined(RP2040)
	SemaphoreHandle_t l { xSemaphoreCreateBinary() };
#else
	std::mutex l;
#endif

public:
	my_lock();
	~my_lock();

	void lock();
	void unlock();
};

class my_unique_lock
{
private:
	my_lock *const l;

public:
	my_unique_lock(my_lock *const l) : l(l) {
		l->lock();
	}

	~my_unique_lock() {
		l->unlock();
	}
};
