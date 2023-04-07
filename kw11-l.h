// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <thread>

#include "bus.h"


class kw11_l
{
private:
	bus         *const b         { nullptr };
	console     *const cnsl      { nullptr };

#if !defined(BUILD_FOR_RP2040)
	std::thread *      th        { nullptr };
#endif
	std::atomic_bool   stop_flag { false };

public:
	kw11_l(bus *const b, console *const cnsl);
	virtual ~kw11_l();

	void operator()();
};
