#include "my_lock.h"


my_lock::my_lock()
{
#if defined(FREERTOS)
        xSemaphoreGive(l);  // initialize
#endif
}

my_lock::~my_lock()
{
#if defined(FREERTOS)
	vSemaphoreDelete(l);
#endif
}

void my_lock::lock()
{
#if defined(FREERTOS)
        xSemaphoreTake(l, portMAX_DELAY);
#else
	l.lock();
#endif
}

void my_lock::unlock()
{
#if defined(FREERTOS)
        xSemaphoreGive(l);
#else
	l.unlock();
#endif
}
