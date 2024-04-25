// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <thread>

#include "bus.h"
#include "console.h"
#include "gen.h"


class kw11_l
{
private:
	bus         *const b          { nullptr };
	console           *cnsl       { nullptr };

#if defined(BUILD_FOR_RP2040)
	SemaphoreHandle_t lf_csr_lock { xSemaphoreCreateBinary() };
#else
	std::thread       *th         { nullptr };
	std::mutex         lf_csr_lock;
#endif
	uint16_t           lf_csr     { 0       };

	std::atomic_bool   stop_flag  { false   };

	uint8_t  get_lf_crs();
	void     set_lf_crs_b7();

public:
	kw11_l(bus *const b);
	virtual ~kw11_l();

	void     reset();

#if IS_POSIX
	json_t *serialize();
	static kw11_l *deserialize(const json_t *const j, bus *const b, console *const cnsl);
#endif

	void     begin(console *const cnsl);
	void     operator()();

	uint16_t readWord (const uint16_t a);
	void     writeWord(const uint16_t a, const uint16_t v);
};
