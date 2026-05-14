#include "my_lock.h"


my_lock::my_lock()
{
#if defined(BUILD_FOR_PICO2W)
        xSemaphoreGive(l);  // initialize
#endif
}

my_lock::~my_lock()
{
#if defined(BUILD_FOR_PICO2W)
	vSemaphoreDelete(l);
#endif
}

void my_lock::lock()
{
#if defined(BUILD_FOR_PICO2W)
        xSemaphoreTake(l, portMAX_DELAY);
#else
	l.lock();
#endif
}

void my_lock::unlock()
{
#if defined(BUILD_FOR_PICO2W)
        xSemaphoreGive(l);
#else
	l.unlock();
#endif
}
