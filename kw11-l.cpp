#include <unistd.h>

#include "console.h"
#include "cpu.h"
#include "kw11-l.h"
#include "utils.h"


kw11_l::kw11_l(bus *const b, console *const cnsl) : b(b), cnsl(cnsl)
{
	th = new std::thread(std::ref(*this));
}

kw11_l::~kw11_l()
{
	stop_flag = true;

	th->join();

	delete th;
}

void kw11_l::operator()()
{
	while(!stop_flag) {
		if (*cnsl->get_running_flag()) {
			b->set_lf_crs_b7();

			if (b->get_lf_crs() & 64)
				b->getCpu()->queue_interrupt(6, 0100);

			myusleep(1000000 / 50);
		}
		else {
			myusleep(1000000 / 10);
		}
	}
}
