// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <unistd.h>

#include "console.h"
#include "cpu.h"
#include "kw11-l.h"
#include "utils.h"

#if defined(ESP32)
#include "esp32.h"
#endif


#if defined(ESP32)
void thread_wrapper_kw11(void *p)
{
	kw11_l *const kw11l = reinterpret_cast<kw11_l *>(p);

	kw11l->operator()();
}
#endif

kw11_l::kw11_l(bus *const b, console *const cnsl) : b(b), cnsl(cnsl)
{
#if defined(ESP32)
	xTaskCreate(&thread_wrapper_kw11, "kw11-l", 2048, this, 1, nullptr);
#else
	th = new std::thread(std::ref(*this));
#endif
}

kw11_l::~kw11_l()
{
	stop_flag = true;

#if !defined(ESP32)
	th->join();
#endif

	delete th;
}

void kw11_l::operator()()
{
	while(!stop_flag) {
		if (*cnsl->get_running_flag()) {
			b->set_lf_crs_b7();

			if (b->get_lf_crs() & 64)
				b->getCpu()->queue_interrupt(6, 0100);

			myusleep(1000000 / 50);  // 20ms
		}
		else {
			myusleep(1000000 / 10);  // 100ms
		}
	}
}
