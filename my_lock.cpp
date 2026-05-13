#include "my_lock.h"


my_lock::my_lock()
{
#if defined(BUILD_FOR_RP2040)
        xSemaphoreGive(l);  // initialize
#endif
}

my_lock::~my_lock()
{
#if defined(BUILD_FOR_RP2040)
	vSemaphoreDelete(l);
#endif
}

void my_lock::lock()
{
#if defined(BUILD_FOR_RP2040)
        xSemaphoreTake(l, portMAX_DELAY);
#else
	l.lock();
#endif
}

void my_lock::unlock()
{
#if defined(BUILD_FOR_RP2040)
        xSemaphoreGive(l);
#else
	l.unlock();
#endif
}
