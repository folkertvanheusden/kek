// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "tm-11.h"
#include "rk05.h"
#include "rl02.h"

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

	uint16_t read_pdr (const uint32_t a, const int run_mode, const bool word_mode, const bool peek_only);
	uint16_t read_par (const uint32_t a, const int run_mode, const bool word_mode, const bool peek_only);
	void     write_pdr(const uint32_t a, const int run_mode, const uint16_t value, const bool word_mode);
	void     write_par(const uint32_t a, const int run_mode, const uint16_t value, const bool word_mode);

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

	cpu *getCpu() { return this->c; }

	tty *getTty() { return this->tty_; }

	void init();  // invoked by 'RESET' command

	void    set_lf_crs_b7();
	uint8_t get_lf_crs();

	uint16_t read(const uint16_t a, const bool word_mode, const bool use_prev, const bool peek_only=false);
	uint16_t readByte(const uint16_t a) { return read(a, true, false); }
	uint16_t readWord(const uint16_t a);
	uint16_t peekWord(const uint16_t a);

	uint16_t readUnibusByte(const uint16_t a);

	void write(const uint16_t a, const bool word_mode, uint16_t value, const bool use_prev);
	void writeByte(const uint16_t a, const uint8_t value) { return write(a, true, value, false); }
	void writeWord(const uint16_t a, const uint16_t value);

	void writeUnibusByte(const uint16_t a, const uint8_t value);

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

	uint32_t calculate_physical_address(const int run_mode, const uint16_t a, const bool trap_on_failure, const bool is_write, const bool peek_only, const bool word_mode);
};
