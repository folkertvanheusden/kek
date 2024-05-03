// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#include <assert.h>
#include <mutex>
#include <stdint.h>
#include <stdio.h>

#include "gen.h"
#include "dc11.h"
#include "mmu.h"
#include "rk05.h"
#include "rl02.h"
#include "tm-11.h"

#if defined(BUILD_FOR_RP2040)
#include "rp2040.h"
#endif

#define ADDR_MMR0 0177572
#define ADDR_MMR1 0177574
#define ADDR_MMR2 0177576
#define ADDR_MMR3 0172516

#define ADDR_PIR  0177772
#define ADDR_LFC  0177546  // line frequency
#define ADDR_MAINT 0177750
#define ADDR_CONSW 0177570
#define ADDR_KW11P 0172540
#define ADDR_LP11CSR 0177514  // printer

#define ADDR_PSW      0177776
#define ADDR_STACKLIM 0177774
#define ADDR_KERNEL_R 0177700
#define ADDR_USER_R   0177710
#define ADDR_KERNEL_SP 0177706
#define ADDR_PC       0177707
#define ADDR_SV_SP    0177716
#define ADDR_USER_SP  0177717

#define ADDR_CPU_ERR 0177766
#define ADDR_SYSSIZE 0177760
#define ADDR_MICROPROG_BREAK_REG 0177770
#define ADDR_CCR 0177746
#define ADDR_SYSTEM_ID 0177764

class console;
class cpu;
class kw11_l;
class memory;
class tm_11;
class tty;

typedef struct {
	bool is_psw;
} write_rc_t;

class bus
{
private:
	cpu     *c       { nullptr };
	tm_11   *tm11    { nullptr };
	rk05    *rk05_   { nullptr };
	rl02    *rl02_   { nullptr };
	tty     *tty_    { nullptr };
	kw11_l  *kw11_l_ { nullptr };
	mmu     *mmu_    { nullptr };
	memory  *m       { nullptr };
	dc11    *dc11_   { nullptr };

	uint16_t microprogram_break_register { 0 };

	uint16_t console_switches { 0 };
	uint16_t console_leds     { 0 };

public:
	bus();
	~bus();

#if IS_POSIX
	json_t *serialize() const;
	static bus *deserialize(const json_t *const j, console *const cnsl, std::atomic_uint32_t *const event);
#endif

	void reset();
	void init();  // invoked by 'RESET' command

	void set_console_switches(const uint16_t new_state) { console_switches = new_state; }
	void set_console_switch(const int bit, const bool state) { console_switches &= ~(1 << bit); console_switches |= state << bit; }
	uint16_t get_console_switches() { return console_switches; }
	void set_debug_mode() { console_switches |= 128; }
	uint16_t get_console_leds() { return console_leds; }

	void set_memory_size(const int n_pages);

	void add_ram   (memory *const m      );
	void add_cpu   (cpu    *const c      );
	void add_mmu   (mmu    *const mmu_   );
	void add_tm11  (tm_11  *const tm11   );
	void add_rk05  (rk05   *const rk05_  );
	void add_rl02  (rl02   *const rl02_  );
	void add_tty   (tty    *const tty_   );
	void add_KW11_L(kw11_l *const kw11_l_);
	void add_DC11  (dc11   *const dc11_  );

	memory *getRAM()    { return m;       }
	cpu    *getCpu()    { return c;       }
	kw11_l *getKW11_L() { return kw11_l_; }
	tty    *getTty()    { return tty_;    }
	mmu    *getMMU()    { return mmu_;    }
	rk05   *getRK05()   { return rk05_;   }
	rl02   *getRL02()   { return rl02_;   }
	dc11   *getDC11()   { return dc11_;   }
	tm_11  *getTM11()   { return tm11;    }

	uint16_t read(const uint16_t a, const word_mode_t word_mode, const rm_selection_t mode_selection, const bool peek_only=false, const d_i_space_t s = i_space);
	uint16_t read_byte(const uint16_t a) { return read(a, wm_byte, rm_cur); }
	uint16_t read_word(const uint16_t a, const d_i_space_t s = i_space);
	uint16_t peekWord(const uint16_t a);
	uint8_t  readUnibusByte(const uint32_t a);
	uint16_t readPhysical(const uint32_t a);

	write_rc_t write(const uint16_t a, const word_mode_t word_mode, uint16_t value, const rm_selection_t mode_selection, const d_i_space_t s = i_space);
	void     writeUnibusByte(const uint32_t a, const uint8_t value);
	void     write_byte(const uint16_t a, const uint8_t value) { write(a, wm_byte, value, rm_cur); }
	void     write_word(const uint16_t a, const uint16_t value, const d_i_space_t s = i_space);
	void     writePhysical(const uint32_t a, const uint16_t value);

	bool     is_psw(const uint16_t addr, const int run_mode, const d_i_space_t space) const;
};
