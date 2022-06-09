#include <atomic>
#include <thread>

#include "bus.h"


class kw11_l
{
private:
	bus         *const b         { nullptr };
	std::thread *      th        { nullptr };
	std::atomic_bool   stop_flag { false };

public:
	kw11_l(bus *const b);
	virtual ~kw11_l();

	void operator()();
};
