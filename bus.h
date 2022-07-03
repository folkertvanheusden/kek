// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "gen.h"
#include "tm-11.h"
#include "rk05.h"
#include "rl02.h"

#define ADDR_MMR0 0777572
#define ADDR_MMR1 0777574
#define ADDR_MMR2 0777576
#define ADDR_MMR3 0772516

#define ADDR_PIR  0777772
#define ADDR_LFC  0777546  // line frequency
#define ADDR_MAINT 0777750
#define ADDR_CONSW 0777570
#define ADDR_KW11P 0772540
#define ADDR_LP11CSR 0777514  // printer

#define ADDR_PDR_SV_START 0772200
#define ADDR_PDR_SV_END   0772240
#define ADDR_PAR_SV_START 0772240
#define ADDR_PAR_SV_END   0772300

#define ADDR_PDR_K_START 0772300
#define ADDR_PDR_K_END   0772340
#define ADDR_PAR_K_START 0772340
#define ADDR_PAR_K_END   0772400

#define ADDR_PDR_U_START 0777600
#define ADDR_PDR_U_END   0777640
#define ADDR_PAR_U_START 0777640
#define ADDR_PAR_U_END   0777700

#define ADDR_PSW      0777776
#define ADDR_STACKLIM 0777774
#define ADDR_KERNEL_R 0777700
#define ADDR_USER_R   0777710
#define ADDR_KERNEL_SP 0777706
#define ADDR_PC       0777707
#define ADDR_SV_SP    0777716
#define ADDR_USER_SP  0777717

#define ADDR_CPU_ERR 0777766
#define ADDR_SYSSIZE 0777760
#define ADDR_MICROPROG_BREAK_REG 0777770
#define ADDR_CCR 0777746

class cpu;
class memory;
class tty;

typedef struct
{
	uint16_t par, pdr;
} page_t;

class bus
{
private:
	cpu     *c     { nullptr };
	tm_11   *tm11  { nullptr };
	rk05    *rk05_ { nullptr };
	rl02    *rl02_ { nullptr };
	tty     *tty_  { nullptr };

	memory  *m     { nullptr };

	// 8 pages, D/I, 3 modes and 1 invalid mode
	page_t   pages[4][2][8];

	uint16_t MMR0 { 0 }, MMR1 { 0 }, MMR2 { 0 }, MMR3 { 0 }, CPUERR { 0 }, PIR { 0 }, CSR { 0 };

	uint16_t switch_register { 0 };

	uint16_t lf_csr { 0 };

	uint16_t console_switches { 0 };

	uint16_t read_pdr (const uint32_t a, const int run_mode, const word_mode_t wm, const bool peek_only);
	uint16_t read_par (const uint32_t a, const int run_mode, const word_mode_t wm, const bool peek_only);
	void     write_pdr(const uint32_t a, const int run_mode, const uint16_t value, const word_mode_t wm);
	void     write_par(const uint32_t a, const int run_mode, const uint16_t value, const word_mode_t wm);

public:
	bus();
	~bus();

	void clearmem();

	void set_console_switches(const uint16_t new_state) { console_switches = new_state; }
	void set_debug_mode() { console_switches |= 128; }

	void add_cpu(cpu *const c) { this -> c = c; }
	void add_tm11(tm_11 *tm11) { this -> tm11 = tm11; } 
	void add_rk05(rk05 *rk05_) { this -> rk05_ = rk05_; } 
	void add_rl02(rl02 *rl02_) { this -> rl02_ = rl02_; }
	void add_tty(tty *tty_)    { this -> tty_ = tty_; }

	cpu     *getCpu() { return this->c; }

	tty     *getTty() { return this->tty_; }

	page_t *get_page_t(int mode, int nr) { return &pages[mode][0][nr]; }

	void     init();  // invoked by 'RESET' command

	void     set_lf_crs_b7();
	uint8_t  get_lf_crs();

	uint16_t peekWord  (const uint16_t a);
	uint16_t read_phys (const uint32_t a, const word_mode_t wm, const bool peek_only=false);
	void     write_phys(const uint32_t a, const word_mode_t wm, const uint16_t value);
	void     register_write(const uint16_t virt_addr, const int run_mode);

	uint16_t read_cur_word(const uint16_t a);
	void     write_cur_word(const uint16_t a, const uint16_t v);
	void     write_cur_byte(const uint16_t a, const uint8_t  v);

	uint16_t getMMR0() { return MMR0; }
	uint16_t getMMR1() { return MMR1; }
	uint16_t getMMR2() { return MMR2; }
	uint16_t getMMR3() { return MMR3; }
	void     clearMMR1();
	void     addToMMR1(const int8_t delta, const uint8_t reg);
	void     setMMR0(int value);
	void     setMMR0Bit(const int bit);
	void     clearMMR0Bit(const int bit);
	void     setMMR2(const uint16_t value);

	uint16_t get_switch_register() const { return switch_register; }

	void     check_bus(const uint16_t av, const word_mode_t wm, const bool is_write, const run_mode_sel_t rms);

	uint32_t virt_to_phys(const uint16_t av, const run_mode_sel_t rms);
};
