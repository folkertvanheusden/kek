// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "gen.h"
#include "cpu.h"
#include "log.h"
#include "memory.h"
#include "tm-11.h"
#include "tty.h"

#if defined(ESP32) || defined(BUILD_FOR_RP2040)
#if defined(ESP32)
#include <esp_debug_helpers.h>
#endif

// ESP32 goes in a crash-loop when allocating 128kB
// see also https://github.com/espressif/esp-idf/issues/1934
constexpr int n_pages = 12;
#else
constexpr int n_pages = 31;  // 30=240kB (for EKBEEx.BIC)
#endif

constexpr const int di_ena_mask[4] = { 4, 2, 0, 1 };

bus::bus()
{
	m = new memory(n_pages * 8192l);

	memset(pages, 0x00, sizeof pages);

	CPUERR = MMR0 = MMR1 = MMR2 = MMR3 = PIR = CSR = 0;

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(lf_csr_lock);  // initialize
#endif
}

bus::~bus()
{
	delete c;
	delete tm11;
	delete rk05_;
	delete rl02_;
	delete tty_;
	delete m;
}

void bus::add_cpu(cpu *const c)
{
	delete this->c;
	this->c     = c;
}

void bus::add_tm11(tm_11 *const tm11)
{
	delete this->tm11;
	this->tm11  = tm11;
} 

void bus::add_rk05(rk05 *const rk05_)
{
	delete this->rk05_;
	this->rk05_ = rk05_;
} 

void bus::add_rl02(rl02 *const rl02_)
{
	delete this->rl02_;
	this->rl02_ = rl02_;
}

void bus::add_tty(tty *const tty_)
{
	delete this->tty_;
	this->tty_  = tty_;
}

void bus::clearmem()
{
	m->reset();
}

void bus::init()
{
	MMR0 = 0;
	MMR3 = 0;
}

uint16_t bus::read_pdr(const uint32_t a, const int run_mode, const word_mode_t word_mode, const bool peek_only)
{
	int      page = (a >> 1) & 7;
	bool     is_d = a & 16;
	uint16_t t    = pages[run_mode][is_d][page].pdr;

	if (!peek_only)
		DOLOG(debug, false, "READ-I/O PDR run-mode %d: %c for %d: %o", run_mode, is_d ? 'D' : 'I', page, t);

	return word_mode ? (a & 1 ? t >> 8 : t & 255) : t;
}

uint16_t bus::read_par(const uint32_t a, const int run_mode, const word_mode_t word_mode, const bool peek_only)
{
	int      page = (a >> 1) & 7;
	bool     is_d = a & 16;
	uint16_t t    = pages[run_mode][is_d][page].par;

	if (!peek_only)
		DOLOG(debug, false, "READ-I/O PAR run-mode %d: %c for %d: %o (phys: %07o)", run_mode, is_d ? 'D' : 'I', page, t, t * 64);

	return word_mode ? (a & 1 ? t >> 8 : t & 255) : t;
}

void bus::trap_odd(const uint16_t a)
{
	MMR0 &= ~(7 << 1);
	MMR0 |= (a >> 13) << 1;

	c->trap(004);  // invalid access
}

uint16_t bus::read_io(const uint16_t addr_in, const word_mode_t word_mode, const bool peek_only)
{
	//// REGISTERS ////
	if (addr_in >= ADDR_KERNEL_R && addr_in <= ADDR_KERNEL_R + 5) { // kernel R0-R5
		uint16_t temp = c->getRegister(addr_in - ADDR_KERNEL_R) & (word_mode ? 0xff : 0xffff);
		if (!peek_only) DOLOG(debug, false, "READ-I/O kernel R%d: %06o", addr_in - ADDR_KERNEL_R, temp);
		return temp;
	}
	if (addr_in >= ADDR_USER_R && addr_in <= ADDR_USER_R + 5) { // user R0-R5
		uint16_t temp = c->getRegister(addr_in - ADDR_USER_R) & (word_mode ? 0xff : 0xffff);
		if (!peek_only) DOLOG(debug, false, "READ-I/O user R%d: %06o", addr_in - ADDR_USER_R, temp);
		return temp;
	}
	if (addr_in == ADDR_KERNEL_SP) { // kernel SP
		uint16_t temp = c->getStackPointer(0) & (word_mode ? 0xff : 0xffff);
		if (!peek_only) DOLOG(debug, false, "READ-I/O kernel SP: %06o", temp);
		return temp;
	}
	if (addr_in == ADDR_PC) { // PC
		uint16_t temp = c->getPC() & (word_mode ? 0xff : 0xffff);
		if (!peek_only) DOLOG(debug, false, "READ-I/O PC: %06o", temp);
		return temp;
	}
	if (addr_in == ADDR_SV_SP) { // supervisor SP
		uint16_t temp = c->getStackPointer(1) & (word_mode ? 0xff : 0xffff);
		if (!peek_only) DOLOG(debug, false, "READ-I/O supervisor SP: %06o", temp);
		return temp;
	}
	if (addr_in == ADDR_USER_SP) { // user SP
		uint16_t temp = c->getStackPointer(3) & (word_mode ? 0xff : 0xffff);
		if (!peek_only) DOLOG(debug, false, "READ-I/O user SP: %06o", temp);
		return temp;
	}
	///^ registers ^///

	if (!peek_only) {
		if ((addr_in & 1) && word_mode == wm_word) {
			DOLOG(debug, true, "READ-I/O odd address %06o UNHANDLED", addr_in);
			trap_odd(addr_in);
			throw 0;
			return 0;
		}
	}

	if (addr_in == ADDR_CPU_ERR) { // cpu error register
		uint16_t temp = CPUERR & 0xff;
		if (!peek_only) DOLOG(debug, false, "READ-I/O CPU error: %03o", temp);
		return temp;
	}

	if (addr_in == ADDR_MAINT) { // MAINT
		uint16_t temp = 1; // POWER OK
		if (!peek_only) DOLOG(debug, false, "READ-I/O MAINT: %o", temp);
		return temp;
	}

	if (addr_in == ADDR_CONSW) { // console switch & display register
		uint16_t temp = console_switches;
		if (!peek_only) DOLOG(debug, false, "READ-I/O console switch: %o", temp);
		return temp;
	}

	if (addr_in == ADDR_KW11P) { // KW11P programmable clock
		uint16_t temp = 128;
		if (!peek_only) DOLOG(debug, false, "READ-I/O programmable clock: %o", temp);
		return temp;
	}

	if (addr_in == ADDR_PIR || addr_in == ADDR_PIR + 1) { // PIR
		uint16_t temp = 0;

		if (word_mode == wm_word)
			temp = PIR;
		else
			temp = addr_in == ADDR_PIR ? PIR & 255 : PIR >> 8;

		if (!peek_only) DOLOG(debug, false, "READ-I/O PIR: %o", temp);
		return temp;
	}

	if (addr_in == ADDR_SYSTEM_ID) {
		uint16_t temp = 011064;
		if (!peek_only) DOLOG(debug, false, "READ-I/O system id: %o", temp);
		return temp;
	}

	if (addr_in == ADDR_LFC) { // line frequency clock and status register
#if defined(BUILD_FOR_RP2040)
		xSemaphoreTake(lf_csr_lock, portMAX_DELAY);
#else
		std::unique_lock<std::mutex> lck(lf_csr_lock);
#endif

		uint16_t temp = lf_csr;
		if (!peek_only) DOLOG(debug, false, "READ-I/O line frequency clock: %o", temp);

#if defined(BUILD_FOR_RP2040)
		xSemaphoreGive(lf_csr_lock);
#endif

		return temp;
	}

	if (addr_in == ADDR_LP11CSR) { // printer, CSR register, LP11
		uint16_t temp = 0x80;
		if (!peek_only) DOLOG(debug, false, "READ-I/O LP11 CSR: %o", temp);
		return temp;
	}

	/// MMU ///
	if (addr_in >= ADDR_PDR_SV_START && addr_in < ADDR_PDR_SV_END)
		return read_pdr(addr_in, 1, word_mode, peek_only);
	else if (addr_in >= ADDR_PAR_SV_START && addr_in < ADDR_PAR_SV_END)
		return read_par(addr_in, 1, word_mode, peek_only);
	else if (addr_in >= ADDR_PDR_K_START && addr_in < ADDR_PDR_K_END)
		return read_pdr(addr_in, 0, word_mode, peek_only);
	else if (addr_in >= ADDR_PAR_K_START && addr_in < ADDR_PAR_K_END)
		return read_par(addr_in, 0, word_mode, peek_only);
	else if (addr_in >= ADDR_PDR_U_START && addr_in < ADDR_PDR_U_END)
		return read_pdr(addr_in, 3, word_mode, peek_only);
	else if (addr_in >= ADDR_PAR_U_START && addr_in < ADDR_PAR_U_END)
		return read_par(addr_in, 3, word_mode, peek_only);
	///////////

	if (addr_in >= 0177740 && addr_in <= 0177753) { // cache control register and others
		if (!peek_only) DOLOG(debug, false, "READ-I/O cache control register/others (%06o): %o", addr_in, 0);
		// TODO
		return 0;
	}

	if (addr_in >= 0170200 && addr_in <= 0170377) { // unibus map
		if (!peek_only) DOLOG(debug, false, "READ-I/O unibus map (%06o): %o", addr_in, 0);
		// TODO
		return 0;
	}

	if (word_mode) {
		if (addr_in == ADDR_PSW) { // PSW
			uint8_t temp = c->getPSW();
			if (!peek_only) DOLOG(debug, false, "READ-I/O PSW LSB: %03o", temp);
			return temp;
		}

		if (addr_in == ADDR_PSW + 1) {
			uint8_t temp = c->getPSW() >> 8;
			if (!peek_only) DOLOG(debug, false, "READ-I/O PSW MSB: %03o", temp);
			return temp;
		}
		if (addr_in == ADDR_STACKLIM) { // stack limit register
			uint8_t temp = c->getStackLimitRegister();
			if (!peek_only) DOLOG(debug, false, "READ-I/O stack limit register (low): %03o", temp);
			return temp;
		}
		if (addr_in == ADDR_STACKLIM + 1) { // stack limit register
			uint8_t temp = c->getStackLimitRegister() >> 8;
			if (!peek_only) DOLOG(debug, false, "READ-I/O stack limit register (high): %03o", temp);
			return temp;
		}

		if (addr_in == ADDR_MICROPROG_BREAK_REG) {  // microprogram break register
			uint8_t temp = microprogram_break_register;
			if (!peek_only) DOLOG(debug, false, "READ-I/O microprogram break register (low): %03o", temp);
			return temp;
		}
		if (addr_in == ADDR_MICROPROG_BREAK_REG + 1) {  // microprogram break register
			uint8_t temp = microprogram_break_register >> 8;
			if (!peek_only) DOLOG(debug, false, "READ-I/O microprogram break register (high): %03o", temp);
			return temp;
		}

		if (addr_in == ADDR_MMR0) {
			uint8_t temp = MMR0;
			if (!peek_only) DOLOG(debug, false, "READ-I/O MMR0 LO: %03o", temp);
			return temp;
		}
		if (addr_in == ADDR_MMR0 + 1) {
			uint8_t temp = MMR0 >> 8;
			if (!peek_only) DOLOG(debug, false, "READ-I/O MMR0 HI: %03o", temp);
			return temp;
		}
	}
	else {
		if (addr_in == ADDR_MMR0) {
			uint16_t temp = MMR0;
			if (!peek_only) DOLOG(debug, false, "READ-I/O MMR0: %06o", temp);
			return temp;
		}

		if (addr_in == ADDR_MMR1) { // MMR1
			uint16_t temp = MMR1;
			if (!peek_only) DOLOG(debug, false, "READ-I/O MMR1: %06o", temp);
			return temp;
		}

		if (addr_in == ADDR_MMR2) { // MMR2
			uint16_t temp = MMR2;
			if (!peek_only) DOLOG(debug, false, "READ-I/O MMR2: %06o", temp);
			return temp;
		}

		if (addr_in == ADDR_MMR3) { // MMR3
			uint16_t temp = MMR3;
			if (!peek_only) DOLOG(debug, false, "READ-I/O MMR3: %06o", temp);
			return temp;
		}

		if (addr_in == ADDR_PSW) { // PSW
			uint16_t temp = c->getPSW();
			if (!peek_only) DOLOG(debug, false, "READ-I/O PSW: %06o", temp);
			return temp;
		}

		if (addr_in == ADDR_STACKLIM) { // stack limit register
			uint16_t temp = c->getStackLimitRegister();
			if (!peek_only) DOLOG(debug, false, "READ-I/O stack limit register: %06o", temp);
			return temp;
		}

		if (addr_in == ADDR_CPU_ERR) { // cpu error register
			uint16_t temp = CPUERR;
			if (!peek_only) DOLOG(debug, false, "READ-I/O CPUERR: %06o", temp);
			return temp;
		}

		if (addr_in == ADDR_MICROPROG_BREAK_REG) {  // microprogram break register
			uint16_t temp = microprogram_break_register;
			if (!peek_only) DOLOG(debug, false, "READ-I/O microprogram break register: %06o", temp);
			return temp;
		}
	}

	if (tm11 && addr_in >= TM_11_BASE && addr_in < TM_11_END && !peek_only) {
		DOLOG(debug, false, "READ-I/O TM11 register %d", (addr_in - TM_11_BASE) / 2);

		return word_mode ? tm11->readByte(addr_in) : tm11->readWord(addr_in);
	}

	if (rk05_ && addr_in >= RK05_BASE && addr_in < RK05_END && !peek_only) {
		DOLOG(debug, false, "READ-I/O RK05 register %d", (addr_in - RK05_BASE) / 2);

		return word_mode ? rk05_->readByte(addr_in) : rk05_->readWord(addr_in);
	}

	if (rl02_ && addr_in >= RL02_BASE && addr_in < RL02_END && !peek_only) {
		DOLOG(debug, false, "READ-I/O RL02 register %d", (addr_in - RL02_BASE) / 2);

		return word_mode ? rl02_->readByte(addr_in) : rl02_->readWord(addr_in);
	}

	if (tty_ && addr_in >= PDP11TTY_BASE && addr_in < PDP11TTY_END && !peek_only) {
		DOLOG(debug, false, "READ-I/O TTY register %d", (addr_in - PDP11TTY_BASE) / 2);

		return word_mode ? tty_->readByte(addr_in) : tty_->readWord(addr_in);
	}

	// LO size register field must be all 1s, so subtract 1
	constexpr uint32_t system_size = n_pages * 8192l / 64 - 1;

	if (addr_in == ADDR_SYSSIZE + 2) {  // system size HI
		uint16_t temp = system_size >> 16;
		if (!peek_only) DOLOG(debug, false, "READ-I/O accessing system size HI: %06o", temp);
		return temp;
	}

	if (addr_in == ADDR_SYSSIZE) {  // system size LO
		uint16_t temp = system_size;
		if (!peek_only) DOLOG(debug, false, "READ-I/O accessing system size LO: %06o", temp);
		return temp;
	}

	if (!peek_only) {
		uint32_t m_offset = addr_in - 0160000 + get_io_base();

		DOLOG(debug, true, "READ-I/O UNHANDLED read %08o (%c), (base: %o)", m_offset, word_mode ? 'B' : ' ', get_io_base());

		c->trap(004);  // no such i/o

		throw 1;
	}

	return -1;
}

uint16_t bus::read(const uint16_t addr_in, const word_mode_t word_mode, const rm_selection_t mode_selection, const bool peek_only, const d_i_space_t space)
{
	int  run_mode     = mode_selection == rm_cur ? c->getPSW_runmode() : c->getPSW_prev_runmode();

	uint32_t m_offset = calculate_physical_address(run_mode, addr_in, !peek_only, false, peek_only, space);

	uint32_t io_base  = get_io_base();
	bool     is_io    = m_offset >= io_base;

	if (is_io) {
		uint16_t a = m_offset - io_base + 0160000;  // TODO

		return read_io(a, word_mode, peek_only);
	}

	if (peek_only == false && word_mode == wm_word && (addr_in & 1)) {
		if (!peek_only) DOLOG(debug, true, "READ from %06o - odd address!", addr_in);
		trap_odd(addr_in);
		throw 2;
		return 0;
	}

	uint16_t temp   = 0;

	if (word_mode)
		temp = m->readByte(m_offset);
	else
		temp = m->readWord(m_offset);

	if (!peek_only) DOLOG(debug, false, "READ from %06o/%07o %c %c: %o (%s)", addr_in, m_offset, space == d_space ? 'D' : 'I', word_mode ? 'B' : 'W', temp, mode_selection == rm_prev ? "prev" : "cur");

	return temp;
}

void bus::setMMR0(uint16_t value)
{
	value &= ~(3 << 10);  // bit 10 & 11 always read as 0

	if (value & 1)
		value &= ~(7l << 13);  // reset error bits

	if (MMR0 & 0160000) {
		if ((value & 1) == 0)
			value &= 254;  // bits 7...1 are protected 
	}

// TODO if bit 15/14/13 are set (either of them), then do not modify bit 1...7

	MMR0 = value;
}

void bus::setMMR0Bit(const int bit)
{
	assert(bit != 10 && bit != 11);
	assert(bit < 16 && bit >= 0);

	MMR0 |= 1 << bit;
}

void bus::clearMMR0Bit(const int bit)
{
	assert(bit != 10 && bit != 11);
	assert(bit < 16 && bit >= 0);

	MMR0 &= ~(1 << bit);
}

void bus::setMMR2(const uint16_t value) 
{
	MMR2 = value;
}

void bus::check_odd_addressing(const uint16_t a, const int run_mode, const d_i_space_t space, const bool is_write)
{
	if (a & 1) {
		if (is_write)
			pages[run_mode][space == d_space][a >> 13].pdr |= 1 << 7;

		trap_odd(a);

		throw 4;
	}
}

memory_addresses_t bus::calculate_physical_address(const int run_mode, const uint16_t a)
{
	const uint8_t apf = a >> 13; // active page field

	if ((MMR0 & 1) == 0) {
		bool is_psw = a == ADDR_PSW;
		return { a, apf, a, is_psw, a, is_psw };
	}

	uint32_t physical_instruction = pages[run_mode][0][apf].par * 64;
	uint32_t physical_data        = pages[run_mode][1][apf].par * 64;

	uint16_t p_offset = a & 8191;  // page offset

	physical_instruction += p_offset;
	physical_data        += p_offset;

	if ((MMR3 & 16) == 0) {  // offset is 18bit
		physical_instruction &= 0x3ffff;
		physical_data        &= 0x3ffff;
	}

	if (get_use_data_space(run_mode) == false)
		physical_data = physical_instruction;

	uint32_t io_base                     = get_io_base();
	bool     physical_instruction_is_psw = (physical_instruction - io_base + 0160000) == ADDR_PSW;
	bool     physical_data_is_psw        = (physical_data        - io_base + 0160000) == ADDR_PSW;

	return { a, apf, physical_instruction, physical_instruction_is_psw, physical_data, physical_data_is_psw };
}

bool bus::get_use_data_space(const int run_mode)
{
	return !!(MMR3 & di_ena_mask[run_mode]);
}

uint32_t bus::calculate_physical_address(const int run_mode, const uint16_t a, const bool trap_on_failure, const bool is_write, const bool peek_only, const d_i_space_t space)
{
	uint32_t m_offset = a;

	if ((MMR0 & 1) || (is_write && (MMR0 & (1 << 8)))) {
		const uint8_t apf = a >> 13; // active page field

		bool          d   = space == d_space && get_use_data_space(run_mode) ? space == d_space : false;

		uint16_t p_offset = a & 8191;  // page offset

		m_offset  = pages[run_mode][d][apf].par * 64;  // memory offset  TODO: handle 16b int-s

		m_offset += p_offset;

		if ((MMR3 & 16) == 0)  // off is 18bit
			m_offset &= 0x3ffff;

		uint32_t io_base  = get_io_base();
		bool     is_io    = m_offset >= io_base;

		if (trap_on_failure) {
			{
				const int access_control = pages[run_mode][d][apf].pdr & 7;

				bool do_trap = false;

				if (is_write && access_control != 6)  // write
					do_trap = true;
				else if (!is_write && (access_control == 0 || access_control == 1 || access_control == 3 || access_control == 4 || access_control == 7)) {  // read
					do_trap = true;
				}

				if (do_trap) {
					bool do_trap_250 = false;

					if ((MMR0 & 0xf000) == 0) {
						DOLOG(debug, true, "TRAP(0250) (throw 5) for access_control %d on address %06o, run mode %d", access_control, a, run_mode);

						do_trap_250 = true;
					}
					else {
						DOLOG(debug, true, "A.C.F. triggger for %d on address %06o, run mode %d", access_control, a, run_mode);
					}

					if (is_write)
						pages[run_mode][d][apf].pdr |= 1 << 7;

					if ((MMR0 & 0160000) == 0) {
						MMR0 &= ~((1l << 15) | (1 << 14) | (1 << 13) | (1 << 12) | (3 << 5) | (7 << 1));

						if (is_write && access_control != 6)
							MMR0 |= 1 << 13;  // read-only
								  //
						if (access_control == 0 || access_control == 4)
							MMR0 |= 1l << 15;  // not resident
						else
							MMR0 |= 1 << 13;  // read-only

						MMR0 |= run_mode << 5;  // TODO: kernel-mode or user-mode when a trap occurs in user-mode?

						MMR0 |= apf << 1; // add current page

						MMR0 |= d << 4;
					}

					DOLOG(debug, true, "MMR0: %06o", MMR0);

					if (do_trap_250) {
						c->trap(0250);  // invalid address

						throw 5;
					}
				}
			}

			if (m_offset >= n_pages * 8192l && !is_io) {
				DOLOG(debug, !peek_only, "bus::calculate_physical_address %o >= %o", m_offset, n_pages * 8192l);
				DOLOG(debug, true, "TRAP(04) (throw 6) on address %06o", a);

				if ((MMR0 & 0160000) == 0) {
					MMR0 &= 017777;
					MMR0 |= 1l << 15;  // non-resident

					MMR0 &= ~14;  // add current page
					MMR0 |= apf << 1;

					MMR0 &= ~(3 << 5);
					MMR0 |= run_mode << 5;
				}

				if (is_write)
					pages[run_mode][d][apf].pdr |= 1 << 7;

				c->trap(04);

				throw 6;
			}

			uint16_t pdr_len = (pages[run_mode][d][apf].pdr >> 8) & 127;
			uint16_t pdr_cmp = (a >> 6) & 127;

			bool direction = pages[run_mode][d][apf].pdr & 8;

			// DOLOG(debug, true, "p_offset %06o pdr_len %06o direction %d, run_mode %d, apf %d, pdr: %06o", p_offset, pdr_len, direction, run_mode, apf, pages[run_mode][d][apf].pdr);

			if ((pdr_cmp > pdr_len && direction == false) || (pdr_cmp < pdr_len && direction == true)) {
				DOLOG(debug, !peek_only, "bus::calculate_physical_address::p_offset %o versus %o direction %d", pdr_cmp, pdr_len, direction);
				DOLOG(debug, true, "TRAP(0250) (throw 7) on address %06o", a);
				c->trap(0250);  // invalid access

				if ((MMR0 & 0160000) == 0) {
					MMR0 &= 017777;
					MMR0 |= 1 << 14;  // length

					MMR0 &= ~14;  // add current page
					MMR0 |= apf << 1;

					MMR0 &= ~(3 << 5);
					MMR0 |= run_mode << 5;

					MMR0 |= d << 4;
				}

				if (is_write)
					pages[run_mode][d][apf].pdr |= 1 << 7;

				throw 7;
			}
		}

		DOLOG(debug, !peek_only, "virtual address %06o maps to physical address %08o (run_mode: %d, apf: %d, par: %08o, poff: %o, AC: %d, %s)", a, m_offset, run_mode, apf, pages[run_mode][d][apf].par * 64, p_offset, pages[run_mode][d][apf].pdr & 7, d ? "D" : "I");
	}
	else {
		// DOLOG(debug, false, "no MMU (read physical address %08o)", m_offset);
	}

	return m_offset;
}

void bus::clearMMR1()
{
	MMR1 = 0;
}

void bus::addToMMR1(const int8_t delta, const uint8_t reg)
{
	assert(reg >= 0 && reg <= 7);
	assert(delta >= -2 && delta <= 2);

	if (getMMR0() & 0160000)  // MMR1 etc are locked
		return;

#if defined(ESP32)
//	if (MMR1 > 255)
//		esp_backtrace_print(32);
#else
	if (MMR1 > 255) {
		extern FILE *lfh;
		fflush(lfh);
	}
	assert(MMR1 < 256);
#endif

	MMR1 <<= 8;

	MMR1 |= (delta & 31) << 3;
	MMR1 |= reg;
}

void bus::write_pdr(const uint32_t a, const int run_mode, const uint16_t value, const word_mode_t word_mode)
{
	bool is_d = a & 16;
	int  page = (a >> 1) & 7;

	if (word_mode) {
		assert(a != 0 || value < 256);

		a & 1 ? (pages[run_mode][is_d][page].pdr &= 0x00ff, pages[run_mode][is_d][page].pdr |= value << 8) :
#ifdef SYSTEM_11_44
			(pages[run_mode][is_d][page].pdr &= 0xff00, pages[run_mode][is_d][page].pdr |= value & ~0361);
#else
			(pages[run_mode][is_d][page].pdr &= 0xff00, pages[run_mode][is_d][page].pdr |= value     );
#endif
	}
	else {
#ifdef SYSTEM_11_44
		pages[run_mode][is_d][page].pdr = value & ~0361;
#else
		pages[run_mode][is_d][page].pdr = value;
#endif
	}

	pages[run_mode][is_d][page].pdr &= ~(32768 + 128 /*A*/ + 64 /*W*/ + 32 + 16);  // set bit 4, 5 & 15 to 0 as they are unused and A/W are set to 0 by writes

	DOLOG(debug, true, "WRITE-I/O PDR run-mode %d: %c for %d: %o [%d]", run_mode, is_d ? 'D' : 'I', page, value, word_mode);
}

void bus::write_par(const uint32_t a, const int run_mode, const uint16_t value, const word_mode_t word_mode)
{
	bool is_d = a & 16;
	int  page = (a >> 1) & 7;

	if (word_mode) {
		a & 1 ? (pages[run_mode][is_d][page].par &= 0x00ff, pages[run_mode][is_d][page].par |= value << 8) :
			(pages[run_mode][is_d][page].par &= 0xff00, pages[run_mode][is_d][page].par |= value     );
	}
	else {
		pages[run_mode][is_d][page].par = value;
	}

	pages[run_mode][is_d][page].pdr &= ~(128 /*A*/ + 64 /*W*/);  // reset PDR A/W when PAR is written to

	DOLOG(debug, true, "WRITE-I/O PAR run-mode %d: %c for %d: %o (%07o)", run_mode, is_d ? 'D' : 'I', page, word_mode ? value & 0xff : value, pages[run_mode][is_d][page].par * 64);
}

void bus::write_io(const uint16_t addr_in, const word_mode_t word_mode, const uint16_t value_in)
{
	uint16_t value = value_in;

	if (word_mode) {
		if (addr_in == ADDR_PSW || addr_in == ADDR_PSW + 1) { // PSW
			DOLOG(debug, true, "WRITE-I/O PSW %s: %03o", addr_in & 1 ? "MSB" : "LSB", value);

			uint16_t vtemp = c->getPSW();

			if (addr_in == ADDR_PSW)
				vtemp = (vtemp & 0xff00) | value;
			else
				vtemp = (vtemp & 0x00ff) | (value << 8);

			vtemp &= ~16;  // cannot set T bit via this

			c->setPSW(vtemp, false);

			return;
		}

		if (addr_in == ADDR_STACKLIM || addr_in == ADDR_STACKLIM + 1) { // stack limit register
			DOLOG(debug, true, "WRITE-I/O stack limit register %s: %03o", addr_in & 1 ? "MSB" : "LSB", value);

			uint16_t v = c->getStackLimitRegister();

			if (addr_in == ADDR_STACKLIM)
				v = (v & 0xff00) | value;
			else
				v = (v & 0x00ff) | (value << 8);

			v |= 0377;

			c->setStackLimitRegister(v);

			return;
		}

		if (addr_in == ADDR_MICROPROG_BREAK_REG || addr_in == ADDR_MICROPROG_BREAK_REG + 1) {  // microprogram break register
			DOLOG(debug, false, "WRITE-I/O micropram break register %s: %03o", addr_in & 1 ? "MSB" : "LSB", value);

			if (addr_in == ADDR_MICROPROG_BREAK_REG)
				microprogram_break_register = (microprogram_break_register & 0xff00) | value;
			else
				microprogram_break_register = (microprogram_break_register & 0x00ff) | (value << 8);

			return;
		}

		if (addr_in == ADDR_MMR0 || addr_in == ADDR_MMR0 + 1) { // MMR0
			DOLOG(debug, true, "WRITE-I/O MMR0 register %s: %03o", addr_in & 1 ? "MSB" : "LSB", value);

			if (addr_in == ADDR_MMR0)
				MMR0 = (MMR0 & 0xff00) | value;
			else
				MMR0 = (MMR0 & 0x00ff) | (value << 8);

			return;
		}
	}
	else {
		if (addr_in == ADDR_PSW) { // PSW
			DOLOG(debug, true, "WRITE-I/O PSW: %06o", value);
			c->setPSW(value & ~16, false);
			return;
		}

		if (addr_in == ADDR_STACKLIM) { // stack limit register
			DOLOG(debug, true, "WRITE-I/O stack limit register: %06o", value);
			c->setStackLimitRegister(value & 0xff00);
			return;
		}

		if (addr_in >= ADDR_KERNEL_R && addr_in <= ADDR_KERNEL_R + 5) { // kernel R0-R5
			int reg = addr_in - ADDR_KERNEL_R;
			DOLOG(debug, true, "WRITE-I/O kernel R%d: %06o", reg, value);
			c->setRegister(reg, value);
			return;
		}
		if (addr_in >= ADDR_USER_R && addr_in <= ADDR_USER_R + 5) { // user R0-R5
			int reg = addr_in - ADDR_USER_R;
			DOLOG(debug, true, "WRITE-I/O user R%d: %06o", reg, value);
			c->setRegister(reg, value);
			return;
		}
		if (addr_in == ADDR_KERNEL_SP) { // kernel SP
			DOLOG(debug, true, "WRITE-I/O kernel SP: %06o", value);
			c->setStackPointer(0, value);
			return;
		}
		if (addr_in == ADDR_PC) { // PC
			DOLOG(debug, true, "WRITE-I/O PC: %06o", value);
			c->setPC(value);
			return;
		}
		if (addr_in == ADDR_SV_SP) { // supervisor SP
			DOLOG(debug, true, "WRITE-I/O supervisor sp: %06o", value);
			c->setStackPointer(1, value);
			return;
		}
		if (addr_in == ADDR_USER_SP) { // user SP
			DOLOG(debug, true, "WRITE-I/O user sp: %06o", value);
			c->setStackPointer(3, value);
			return;
		}

		if (addr_in == ADDR_MICROPROG_BREAK_REG) {  // microprogram break register
			DOLOG(debug, false, "WRITE-I/O microprogram break register: %06o", value);
			microprogram_break_register = value & 0xff; // only 8b on 11/70?
			return;
		}
	}

	if (addr_in == ADDR_CPU_ERR) { // cpu error register
		DOLOG(debug, true, "WRITE-I/O CPUERR: %06o", value);
		CPUERR = 0;
		return;
	}

	if (addr_in == ADDR_MMR3) { // MMR3
		DOLOG(debug, true, "WRITE-I/O set MMR3: %06o", value);
		MMR3 = value;
		return;
	}

	if (addr_in == ADDR_MMR0) { // MMR0
		DOLOG(debug, true, "WRITE-I/O set MMR0: %06o", value);
		setMMR0(value);
		return;
	}

	if (addr_in == ADDR_PIR) { // PIR
		DOLOG(debug, true, "WRITE-I/O set PIR: %06o", value);

		value &= 0177000;

		int bits = value >> 9;

		while(bits) {
			value += 042;  // bit 1...3 and 5...7
			bits >>= 1;
		}

		PIR = value;
		return;
	}

	if (addr_in == ADDR_LFC) { // line frequency clock and status register
#if defined(BUILD_FOR_RP2040)
		xSemaphoreTake(lf_csr_lock, portMAX_DELAY);
#else
		std::unique_lock<std::mutex> lck(lf_csr_lock);
#endif

		DOLOG(debug, true, "WRITE-I/O set line frequency clock/status register: %06o", value);
		lf_csr = value;
#if defined(BUILD_FOR_RP2040)
		xSemaphoreGive(lf_csr_lock);
#endif
		return;
	}

	if (tm11 && addr_in >= TM_11_BASE && addr_in < TM_11_END) {
		DOLOG(debug, false, "WRITE-I/O TM11 register %d: %06o", (addr_in - TM_11_BASE) / 2, value);
		word_mode ? tm11->writeByte(addr_in, value) : tm11->writeWord(addr_in, value);
		return;
	}

	if (rk05_ && addr_in >= RK05_BASE && addr_in < RK05_END) {
		DOLOG(debug, false, "WRITE-I/O RK05 register %d: %06o", (addr_in - RK05_BASE) / 2, value);
		word_mode ? rk05_->writeByte(addr_in, value) : rk05_->writeWord(addr_in, value);
		return;
	}

	if (rl02_ && addr_in >= RL02_BASE && addr_in < RL02_END) {
		DOLOG(debug, false, "WRITE-I/O RL02 register %d: %06o", (addr_in - RL02_BASE) / 2, value);
		word_mode ? rl02_->writeByte(addr_in, value) : rl02_->writeWord(addr_in, value);
		return;
	}

	if (tty_ && addr_in >= PDP11TTY_BASE && addr_in < PDP11TTY_END) {
		DOLOG(debug, false, "WRITE-I/O TTY register %d: %06o", (addr_in - PDP11TTY_BASE) / 2, value);
		word_mode ? tty_->writeByte(addr_in, value) : tty_->writeWord(addr_in, value);
		return;
	}

	/// MMU ///
	// supervisor
	if (addr_in >= ADDR_PDR_SV_START && addr_in < ADDR_PDR_SV_END) {
		write_pdr(addr_in, 1, value, word_mode);
		return;
	}
	if (addr_in >= ADDR_PAR_SV_START && addr_in < ADDR_PAR_SV_END) {
		write_par(addr_in, 1, value, word_mode);
		return;
	}

	// kernel
	if (addr_in >= ADDR_PDR_K_START && addr_in < ADDR_PDR_K_END) {
		write_pdr(addr_in, 0, value, word_mode);
		return;
	}
	if (addr_in >= ADDR_PAR_K_START && addr_in < ADDR_PAR_K_END) {
		write_par(addr_in, 0, value, word_mode);
		return;
	}

	// user
	if (addr_in >= ADDR_PDR_U_START && addr_in < ADDR_PDR_U_END) {
		write_pdr(addr_in, 3, value, word_mode);
		return;
	}
	if (addr_in >= ADDR_PAR_U_START && addr_in < ADDR_PAR_U_END) {
		write_par(addr_in, 3, value, word_mode);
		return;
	}
	////

	if (addr_in >= 0177740 && addr_in <= 0177753) { // cache control register and others
					    // TODO
		return;
	}

	if (addr_in >= 0170200 && addr_in <= 0170377) { // unibus map
		DOLOG(debug, false, "writing %06o to unibus map (%06o)", value, addr_in);
		// TODO
		return;
	}

	if (addr_in == ADDR_CONSW) {  // switch register
		console_leds = value;
		return;
	}

	if (addr_in == ADDR_SYSSIZE || addr_in == ADDR_SYSSIZE + 2)  // system size (is read-only)
		return;

	if (addr_in == ADDR_SYSTEM_ID)  // is r/o
		return;

	///////////

	uint32_t m_offset = addr_in - 0160000 + get_io_base();

	DOLOG(debug, true, "WRITE-I/O UNHANDLED %08o(%c): %06o (base: %o)", m_offset, word_mode ? 'B' : 'W', value, get_io_base());

	if (word_mode == wm_word && (addr_in & 1)) {
		DOLOG(debug, true, "WRITE-I/O to %08o (value: %06o) - odd address!", m_offset, value);

		trap_odd(addr_in);
		throw 8;
	}

	c->trap(004);  // no such i/o
	throw 9;
}

void bus::write(const uint16_t addr_in, const word_mode_t word_mode, uint16_t value, const rm_selection_t mode_selection, const d_i_space_t space)
{
	int           run_mode = mode_selection == rm_cur ? c->getPSW_runmode() : c->getPSW_prev_runmode();

	const uint8_t apf      = addr_in >> 13; // active page field

	bool          is_data  = space == d_space;

	bool          d        = is_data && get_use_data_space(run_mode) ? is_data : false;

	if ((MMR0 & 1) == 1 && (addr_in & 1) == 0 && addr_in != ADDR_MMR0)
                pages[run_mode][d][apf].pdr |= 64;  // set 'W' (written to) bit

	uint32_t m_offset = calculate_physical_address(run_mode, addr_in, true, true, false, space);

	uint32_t io_base  = get_io_base();
	bool     is_io    = m_offset >= io_base;

	if (is_io) {
		uint16_t a = m_offset - io_base + 0160000;  // TODO

		write_io(a, word_mode, value);
	}

	if (word_mode == wm_word && (addr_in & 1)) {
		DOLOG(debug, true, "WRITE to %06o (value: %06o) - odd address!", addr_in, value);

		trap_odd(addr_in);
		throw 10;
	}

	DOLOG(debug, true, "WRITE to %06o/%07o %c %c: %06o", addr_in, m_offset, space == d_space ? 'D' : 'I', word_mode ? 'B' : 'W', value);

	if (word_mode)
		m->writeByte(m_offset, value);
	else
		m->writeWord(m_offset, value);
}

void bus::writePhysical(const uint32_t a, const uint16_t value)
{
	DOLOG(debug, true, "physicalWRITE %06o to %o", value, a);

	if (a >= n_pages * 8192l) {
		DOLOG(debug, true, "physicalWRITE to %o: trap 004", a);
		c->trap(004);
		throw 12;
	}
	else {
		m->writeWord(a, value);
	}
}

uint16_t bus::readPhysical(const uint32_t a)
{
	if (a >= n_pages * 8192l) {
		DOLOG(debug, true, "physicalREAD from %o: trap 004", a);
		c->trap(004);
		throw 13;
	}

	uint16_t value = m->readWord(a);

	DOLOG(debug, true, "physicalREAD %06o from %o", value, a);

	return value;
}

uint16_t bus::readWord(const uint16_t a, const d_i_space_t s)
{
	return read(a, wm_word, rm_cur, false, s);
}

uint16_t bus::peekWord(const uint16_t a)
{
	return read(a, wm_word, rm_cur, true);
}

void bus::writeWord(const uint16_t a, const uint16_t value, const d_i_space_t s)
{
	write(a, wm_word, value, rm_cur, s);
}

uint16_t bus::readUnibusByte(const uint16_t a)
{
	return m->readByte(a);
}

void bus::writeUnibusByte(const uint16_t a, const uint8_t v)
{
	m->writeByte(a, v);
}

void bus::set_lf_crs_b7()
{
#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(lf_csr_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(lf_csr_lock);
#endif

	lf_csr |= 128;

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(lf_csr_lock);
#endif
}

uint8_t bus::get_lf_crs()
{
#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(lf_csr_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(lf_csr_lock);
#endif

	uint8_t rc = lf_csr;

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(lf_csr_lock);
#endif

	return rc;
}
