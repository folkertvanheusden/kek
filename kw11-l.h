// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <ArduinoJson.h>
#include <thread>

#include "bus.h"
#include "console.h"
#include "device.h"
#include "my_lock.h"


class kw11_l: public device
{
private:
	bus         *const b          { nullptr };
	console           *cnsl       { nullptr };

	my_lock            lc_csr_lock;
#if !defined(FREERTOS) && !defined(ESP32)
	std::thread       *th         { nullptr };
#endif
	aint               int_frequency { 50   };
	uint16_t           lf_csr     { 0       };
	bool               wall_clock { true    };

	uint64_t           total_ticks   { 0       };
	uint64_t           enabled_ticks { 0       };
	uint64_t           int_triggered { 0       };

	abool              stop_flag  { false   };

	uint8_t  get_lf_crs();
	void     set_lf_crs_b7();

	void     do_interrupt();

public:
	kw11_l(bus *const b);
	virtual ~kw11_l();

	void     reset(const bool hard) override;

	void     show_state(console *const cnsl) const override;

	int      get_interrupt_frequency();
	void     set_interrupt_frequency(const int Hz);

	JsonDocument serialize();
	static kw11_l *deserialize(const JsonVariantConst j, bus *const b, console *const cnsl);

	void     begin(console *const cnsl);
	void     tick();
	void     operator()();

	uint8_t  read_byte(const uint16_t a) override;
	uint16_t read_word(const uint16_t a) override;

	void     write_byte(const uint16_t addr, const uint8_t  v) override;
	void     write_word(const uint16_t addr, const uint16_t v) override;
};
