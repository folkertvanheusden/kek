// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <stdint.h>
#include <stdio.h>

#include "tm-11.h"
#include "rk05.h"
#include "rx02.h"

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
	cpu    *c     { nullptr };
	tm_11  *tm11  { nullptr };
	rk05   *rk05_ { nullptr };
	rx02   *rx02_ { nullptr };
	tty    *tty_  { nullptr };

	memory *m     { nullptr };

	page_t  pages[4][16];

	uint16_t MMR0 { 0 }, MMR1 { 0 }, MMR2 { 0 }, MMR3 { 0 }, CPUERR { 0 }, PIR { 0 }, CSR { 0 };

	uint16_t switch_register { 0 };

public:
	bus();
	~bus();

	void clearmem();

	void add_cpu(cpu *const c) { this -> c = c; }
	void add_tm11(tm_11 *tm11) { this -> tm11 = tm11; } 
	void add_rk05(rk05 *rk05_) { this -> rk05_ = rk05_; } 
	void add_rx02(rx02 *rx02_) { this -> rx02_ = rx02_; }
	void add_tty(tty *tty_) { this -> tty_ = tty_; }

	cpu *getCpu() { return this->c; }

	tty *getTty() { return this->tty_; }

	uint16_t read(const uint16_t a, const bool word_mode, const bool use_prev=false);
	uint16_t readByte(const uint16_t a) { return read(a, true); }
	uint16_t readWord(const uint16_t a);

	uint16_t readUnibusByte(const uint16_t a);

	uint16_t write(const uint16_t a, const bool word_mode, uint16_t value, const bool use_prev=false);
	uint8_t writeByte(const uint16_t a, const uint8_t value) { return write(a, true, value); }
	uint16_t writeWord(const uint16_t a, const uint16_t value);

	void writeUnibusByte(const uint16_t a, const uint8_t value);

	void setMMR2(const uint16_t value) { MMR2 = value; }

	uint16_t get_switch_register() const { return switch_register; }

	uint32_t calculate_full_address(const uint16_t a);
};
