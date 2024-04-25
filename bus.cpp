// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "gen.h"
#include "cpu.h"
#include "kw11-l.h"
#include "log.h"
#include "memory.h"
#include "mmu.h"
#include "tm-11.h"
#include "tty.h"
#include "utils.h"

#if defined(ESP32)
#include <esp_debug_helpers.h>
#endif


bus::bus()
{
	mmu_ = new mmu();

	kw11_l_ = new kw11_l(this);

	reset();
}

bus::~bus()
{
	delete kw11_l_;
	delete c;
	delete tm11;
	delete rk05_;
	delete rl02_;
	delete tty_;
	delete mmu_;
	delete m;
}

#if IS_POSIX
json_t *bus::serialize()
{
	json_t *j_out = json_object();

	if (m)
		json_object_set(j_out, "memory", m->serialize());

	if (kw11_l_)
		json_object_set(j_out, "kw11-l", kw11_l_->serialize());

	if (tty_)
		json_object_set(j_out, "tty", tty_->serialize());

	if (mmu_)
		json_object_set(j_out, "mmu", mmu_->serialize());

	// TODO: cpu, rl02, rk05, tm11

	return j_out;
}

bus *bus::deserialize(const json_t *const j, console *const cnsl)
{
	bus *b = new bus();

	json_t *temp = nullptr;

	temp = json_object_get(j, "memory");
	if (temp) {
		memory *m = memory::deserialize(temp);
		b->add_ram(m);
	}

	temp = json_object_get(j, "kw11-l");
	if (temp) {
		kw11_l *kw11_l_ = kw11_l::deserialize(temp, b, cnsl);
		b->add_KW11_L(kw11_l_);
	}

	temp = json_object_get(j, "tty");
	if (temp) {
		tty *tty_ = tty::deserialize(temp, b, cnsl);
		b->add_tty(tty_);
	}

	temp = json_object_get(j, "mmu");
	if (temp) {
		mmu *mmu_ = mmu::deserialize(temp);
		b->add_mmu(mmu_);
	}

	// TODO: mmu, cpu, rl02, rk05, tm11

	return b;
}
#endif

void bus::set_memory_size(const int n_pages)
{
	this->n_pages = n_pages;

	uint32_t n_bytes = n_pages * 8192l;

	delete m;
	m = new memory(n_bytes);

	DOLOG(info, false, "Memory is now %u kB in size", n_bytes / 1024);
}

void bus::reset()
{
	if (m)
		m->reset();
	if (mmu_)
		mmu_->reset();
	if (c)
		c->reset();
	if (tm11)
		tm11->reset();
	if (rk05_)
		rk05_->reset();
	if (rl02_)
		rl02_->reset();
	if (tty_)
		tty_->reset();
	if (kw11_l_)
		kw11_l_->reset();
}

void bus::add_KW11_L(kw11_l *const kw11_l_)
{
	delete this->kw11_l_;
	this->kw11_l_ = kw11_l_;
}

void bus::add_ram(memory *const m)
{
	delete this->m;
	this->m = m;
}

void bus::add_mmu(mmu *const mmu_)
{
	delete this->mmu_;
	this->mmu_ = mmu_;
}

void bus::add_cpu(cpu *const c)
{
	delete this->c;
	this->c = c;
}

void bus::add_tm11(tm_11 *const tm11)
{
	delete this->tm11;
	this->tm11= tm11;
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

void bus::init()
{
	mmu_->setMMR0(0);
	mmu_->setMMR3(0);
}

void bus::trap_odd(const uint16_t a)
{
	uint16_t temp = mmu_->getMMR0();

	temp &= ~(7 << 1);
	temp |= (a >> 13) << 1;

	mmu_->setMMR0(temp);

	c->trap(004);  // invalid access
}

uint16_t bus::read(const uint16_t addr_in, const word_mode_t word_mode, const rm_selection_t mode_selection, const bool peek_only, const d_i_space_t space)
{
	int  run_mode     = mode_selection == rm_cur ? c->getPSW_runmode() : c->getPSW_prev_runmode();

	uint32_t m_offset = calculate_physical_address(run_mode, addr_in, !peek_only, false, peek_only, space);

	uint32_t io_base  = get_io_base();
	bool     is_io    = m_offset >= io_base;

	if (is_io) {
		uint16_t a = m_offset - io_base + 0160000;  // TODO

		//// REGISTERS ////
		if (a >= ADDR_KERNEL_R && a <= ADDR_KERNEL_R + 5) { // kernel R0-R5
			uint16_t temp = c->getRegister(a - ADDR_KERNEL_R) & (word_mode == wm_byte ? 0xff : 0xffff);
			if (!peek_only) DOLOG(debug, false, "READ-I/O kernel R%d: %06o", a - ADDR_KERNEL_R, temp);
			return temp;
		}
		if (a >= ADDR_USER_R && a <= ADDR_USER_R + 5) { // user R0-R5
			uint16_t temp = c->getRegister(a - ADDR_USER_R) & (word_mode == wm_byte ? 0xff : 0xffff);
			if (!peek_only) DOLOG(debug, false, "READ-I/O user R%d: %06o", a - ADDR_USER_R, temp);
			return temp;
		}
		if (a == ADDR_KERNEL_SP) { // kernel SP
			uint16_t temp = c->getStackPointer(0) & (word_mode == wm_byte ? 0xff : 0xffff);
			if (!peek_only) DOLOG(debug, false, "READ-I/O kernel SP: %06o", temp);
			return temp;
		}
		if (a == ADDR_PC) { // PC
			uint16_t temp = c->getPC() & (word_mode == wm_byte ? 0xff : 0xffff);
			if (!peek_only) DOLOG(debug, false, "READ-I/O PC: %06o", temp);
			return temp;
		}
		if (a == ADDR_SV_SP) { // supervisor SP
			uint16_t temp = c->getStackPointer(1) & (word_mode == wm_byte ? 0xff : 0xffff);
			if (!peek_only) DOLOG(debug, false, "READ-I/O supervisor SP: %06o", temp);
			return temp;
		}
		if (a == ADDR_USER_SP) { // user SP
			uint16_t temp = c->getStackPointer(3) & (word_mode == wm_byte ? 0xff : 0xffff);
			if (!peek_only) DOLOG(debug, false, "READ-I/O user SP: %06o", temp);
			return temp;
		}
		///^ registers ^///

		if (!peek_only) {
			if ((a & 1) && word_mode == wm_word) {
				DOLOG(debug, false, "READ-I/O odd address %06o UNHANDLED", a);
				trap_odd(a);
				throw 0;
				return 0;
			}
		}

		if (a == ADDR_CPU_ERR) { // cpu error register
			uint16_t temp = mmu_->getCPUERR() & 0xff;
			if (!peek_only) DOLOG(debug, false, "READ-I/O CPU error: %03o", temp);
			return temp;
		}

		if (a == ADDR_MAINT) { // MAINT
			uint16_t temp = 1; // POWER OK
			if (!peek_only) DOLOG(debug, false, "READ-I/O MAINT: %o", temp);
			return temp;
		}

		if (a == ADDR_CONSW) { // console switch & display register
			uint16_t temp = console_switches;
			if (!peek_only) DOLOG(debug, false, "READ-I/O console switch: %o", temp);
			return temp;
		}

		if (a == ADDR_KW11P) { // KW11P programmable clock
			uint16_t temp = 128;
			if (!peek_only) DOLOG(debug, false, "READ-I/O programmable clock: %o", temp);
			return temp;
		}

		if (a == ADDR_PIR || a == ADDR_PIR + 1) { // PIR
			uint16_t temp = 0;

			uint16_t PIR  = mmu_->getPIR();

			if (word_mode == wm_word)
				temp = PIR;
			else
				temp = a == ADDR_PIR ? PIR & 255 : PIR >> 8;

			if (!peek_only) DOLOG(debug, false, "READ-I/O PIR: %o", temp);
			return temp;
		}

		if (a == ADDR_SYSTEM_ID) {
			uint16_t temp = 011064;
			if (!peek_only) DOLOG(debug, false, "READ-I/O system id: %o", temp);
			return temp;
		}

		if (a == ADDR_LFC) // line frequency clock and status register
			return kw11_l_->readWord(a);

		if (a == ADDR_LP11CSR) { // printer, CSR register, LP11
			uint16_t temp = 0x80;
			if (!peek_only) DOLOG(debug, false, "READ-I/O LP11 CSR: %o", temp);
			return temp;
		}

		/// MMU ///
		if ((a >= ADDR_PDR_SV_START && a < ADDR_PDR_SV_END) ||
				(a >= ADDR_PAR_SV_START && a < ADDR_PAR_SV_END) ||
				(a >= ADDR_PDR_K_START && a < ADDR_PDR_K_END) ||
				(a >= ADDR_PAR_K_START && a < ADDR_PAR_K_END) ||
				(a >= ADDR_PDR_U_START && a < ADDR_PDR_U_END) ||
				(a >= ADDR_PAR_U_START && a < ADDR_PAR_U_END)) {
			if (word_mode == wm_word)
				return mmu_->readWord(a);

			return mmu_->readByte(a);
		}
		///////////

		if (a >= 0177740 && a <= 0177753) { // cache control register and others
			if (!peek_only) DOLOG(debug, false, "READ-I/O cache control register/others (%06o): %o", a, 0);
			// TODO
			return 0;
		}

		if (a >= 0170200 && a <= 0170377) { // unibus map
			if (!peek_only) DOLOG(debug, false, "READ-I/O unibus map (%06o): %o", a, 0);
			// TODO
			return 0;
		}

		if (a >= 0172100 && a <= 0172137) {  // MM11-LP parity
			if (!peek_only) DOLOG(debug, false, "READ-I/O MM11-LP parity (%06o): %o", a, 1);
			return 1;
		}

		if (word_mode == wm_byte) {
			if (a == ADDR_PSW) { // PSW
				uint8_t temp = c->getPSW();
				if (!peek_only) DOLOG(debug, false, "READ-I/O PSW LSB: %03o", temp);
				return temp;
			}

			if (a == ADDR_PSW + 1) {
				uint8_t temp = c->getPSW() >> 8;
				if (!peek_only) DOLOG(debug, false, "READ-I/O PSW MSB: %03o", temp);
				return temp;
			}
			if (a == ADDR_STACKLIM) { // stack limit register
				uint8_t temp = c->getStackLimitRegister();
				if (!peek_only) DOLOG(debug, false, "READ-I/O stack limit register (low): %03o", temp);
				return temp;
			}
			if (a == ADDR_STACKLIM + 1) { // stack limit register
				uint8_t temp = c->getStackLimitRegister() >> 8;
				if (!peek_only) DOLOG(debug, false, "READ-I/O stack limit register (high): %03o", temp);
				return temp;
			}

			if (a == ADDR_MICROPROG_BREAK_REG) {  // microprogram break register
				uint8_t temp = microprogram_break_register;
				if (!peek_only) DOLOG(debug, false, "READ-I/O microprogram break register (low): %03o", temp);
				return temp;
			}
			if (a == ADDR_MICROPROG_BREAK_REG + 1) {  // microprogram break register
				uint8_t temp = microprogram_break_register >> 8;
				if (!peek_only) DOLOG(debug, false, "READ-I/O microprogram break register (high): %03o", temp);
				return temp;
			}

			if (a == ADDR_MMR0) {
				uint8_t temp = mmu_->getMMR0();
				if (!peek_only) DOLOG(debug, false, "READ-I/O MMR0 LO: %03o", temp);
				return temp;
			}
			if (a == ADDR_MMR0 + 1) {
				uint8_t temp = mmu_->getMMR0() >> 8;
				if (!peek_only) DOLOG(debug, false, "READ-I/O MMR0 HI: %03o", temp);
				return temp;
			}
		}
		else {
			if (a == ADDR_MMR0) {
				uint16_t temp = mmu_->getMMR0();
				if (!peek_only) DOLOG(debug, false, "READ-I/O MMR0: %06o", temp);
				return temp;
			}

			if (a == ADDR_MMR1) { // MMR1
				uint16_t temp = mmu_->getMMR1();
				if (!peek_only) DOLOG(debug, false, "READ-I/O MMR1: %06o", temp);
				return temp;
			}

			if (a == ADDR_MMR2) { // MMR2
				uint16_t temp = mmu_->getMMR2();
				if (!peek_only) DOLOG(debug, false, "READ-I/O MMR2: %06o", temp);
				return temp;
			}

			if (a == ADDR_MMR3) { // MMR3
				uint16_t temp = mmu_->getMMR3();
				if (!peek_only) DOLOG(debug, false, "READ-I/O MMR3: %06o", temp);
				return temp;
			}

			if (a == ADDR_PSW) { // PSW
				uint16_t temp = c->getPSW();
				if (!peek_only) DOLOG(debug, false, "READ-I/O PSW: %06o", temp);
				return temp;
			}

			if (a == ADDR_STACKLIM) { // stack limit register
				uint16_t temp = c->getStackLimitRegister();
				if (!peek_only) DOLOG(debug, false, "READ-I/O stack limit register: %06o", temp);
				return temp;
			}

			if (a == ADDR_CPU_ERR) { // cpu error register
				uint16_t temp = mmu_->getCPUERR();
				if (!peek_only) DOLOG(debug, false, "READ-I/O CPUERR: %06o", temp);
				return temp;
			}

			if (a == ADDR_MICROPROG_BREAK_REG) {  // microprogram break register
				uint16_t temp = microprogram_break_register;
				if (!peek_only) DOLOG(debug, false, "READ-I/O microprogram break register: %06o", temp);
				return temp;
			}
		}

		if (tm11 && a >= TM_11_BASE && a < TM_11_END && !peek_only) {
			DOLOG(debug, false, "READ-I/O TM11 register %d", (a - TM_11_BASE) / 2);

			return word_mode == wm_byte ? tm11->readByte(a) : tm11->readWord(a);
		}

		if (rk05_ && a >= RK05_BASE && a < RK05_END && !peek_only) {
			DOLOG(debug, false, "READ-I/O RK05 register %d", (a - RK05_BASE) / 2);

			return word_mode == wm_byte ? rk05_->readByte(a) : rk05_->readWord(a);
		}

		if (rl02_ && a >= RL02_BASE && a < RL02_END && !peek_only) {
			DOLOG(debug, false, "READ-I/O RL02 register %d", (a - RL02_BASE) / 2);

			return word_mode == wm_byte ? rl02_->readByte(a) : rl02_->readWord(a);
		}

		if (tty_ && a >= PDP11TTY_BASE && a < PDP11TTY_END && !peek_only) {
			DOLOG(debug, false, "READ-I/O TTY register %d", (a - PDP11TTY_BASE) / 2);

			return word_mode == wm_byte ? tty_->readByte(a) : tty_->readWord(a);
		}

		// LO size register field must be all 1s, so subtract 1
		uint32_t system_size = n_pages * 8192l / 64 - 1;

		if (a == ADDR_SYSSIZE + 2) {  // system size HI
			uint16_t temp = system_size >> 16;
			if (!peek_only) DOLOG(debug, false, "READ-I/O accessing system size HI: %06o", temp);
			return temp;
		}

		if (a == ADDR_SYSSIZE) {  // system size LO
			uint16_t temp = system_size;
			if (!peek_only) DOLOG(debug, false, "READ-I/O accessing system size LO: %06o", temp);
			return temp;
		}

		if (!peek_only) {
			DOLOG(debug, false, "READ-I/O UNHANDLED read %08o (%c), (base: %o)", m_offset, word_mode == wm_byte ? 'B' : ' ', get_io_base());

			c->trap(004);  // no such i/o
			throw 1;
		}

		return -1;
	}

	if (peek_only == false && word_mode == wm_word && (addr_in & 1)) {
		if (!peek_only) DOLOG(debug, false, "READ from %06o - odd address!", addr_in);
		trap_odd(addr_in);
		throw 2;
		return 0;
	}

	if (m_offset >= uint32_t(n_pages * 8192)) {
		if (peek_only) {
			DOLOG(debug, false, "READ from %06o - out of range!", addr_in);
			return 0;
		}

		c->trap(004);  // no such RAM
		throw 1;
	}

	uint16_t temp   = 0;
	if (word_mode == wm_byte)
		temp = m->readByte(m_offset);
	else
		temp = m->readWord(m_offset);

	if (!peek_only) DOLOG(debug, false, "READ from %06o/%07o %c %c: %o (%s)", addr_in, m_offset, space == d_space ? 'D' : 'I', word_mode == wm_byte ? 'B' : 'W', temp, mode_selection == rm_prev ? "prev" : "cur");

	return temp;
}

void bus::check_odd_addressing(const uint16_t a, const int run_mode, const d_i_space_t space, const bool is_write)
{
	if (a & 1) {
		if (is_write)
			mmu_->set_page_trapped(run_mode, space == d_space, a >> 13);

		trap_odd(a);

		throw 4;
	}
}

memory_addresses_t bus::calculate_physical_address(const int run_mode, const uint16_t a) const
{
	const uint8_t apf = a >> 13; // active page field

	if (mmu_->is_enabled() == false) {
		bool is_psw = a == ADDR_PSW;
		return { a, apf, a, is_psw, a, is_psw };
	}

	uint32_t physical_instruction = mmu_->get_physical_memory_offset(run_mode, 0, apf);
	uint32_t physical_data        = mmu_->get_physical_memory_offset(run_mode, 1, apf);

	uint16_t p_offset = a & 8191;  // page offset

	physical_instruction += p_offset;
	physical_data        += p_offset;

	if ((mmu_->getMMR3() & 16) == 0) {  // offset is 18bit
		physical_instruction &= 0x3ffff;
		physical_data        &= 0x3ffff;
	}

	if (mmu_->get_use_data_space(run_mode) == false)
		physical_data = physical_instruction;

	uint32_t io_base                     = get_io_base();
	bool     physical_instruction_is_psw = (physical_instruction - io_base + 0160000) == ADDR_PSW;
	bool     physical_data_is_psw        = (physical_data        - io_base + 0160000) == ADDR_PSW;

	return { a, apf, physical_instruction, physical_instruction_is_psw, physical_data, physical_data_is_psw };
}

bool bus::is_psw(const uint16_t addr, const int run_mode, const d_i_space_t space) const
{
	auto meta = calculate_physical_address(run_mode, addr);

	if (space == d_space && meta.physical_data_is_psw)
		return true;

	if (space == i_space && meta.physical_instruction_is_psw)
		return true;

	return false;
}

void bus::mmudebug(const uint16_t a)
{
	for(int rm=0; rm<4; rm++) {
		auto ma = calculate_physical_address(rm, a);

		DOLOG(debug, false, "RM %d, a: %06o, apf: %d, PI: %08o (PSW: %d), PD: %08o (PSW: %d)", rm, ma.virtual_address, ma.apf, ma.physical_instruction, ma.physical_instruction_is_psw, ma.physical_data, ma.physical_data_is_psw);
	}
}

std::pair<trap_action_t, int> bus::get_trap_action(const int run_mode, const bool d, const int apf, const bool is_write)
{
	const int access_control = mmu_->get_access_control(run_mode, d, apf);

	trap_action_t trap_action = T_PROCEED;

	if (access_control == 0)
		trap_action = T_ABORT_4;
	else if (access_control == 1)
		trap_action = is_write ? T_ABORT_4 : T_TRAP_250;
	else if (access_control == 2) {
		if (is_write)
			trap_action = T_ABORT_4;
	}
	else if (access_control == 3)
		trap_action = T_ABORT_4;
	else if (access_control == 4)
		trap_action = T_TRAP_250;
	else if (access_control == 5) {
		if (is_write)
			trap_action = T_TRAP_250;
	}
	else if (access_control == 6) {
		// proceed
	}
	else if (access_control == 7) {
		trap_action = T_ABORT_4;
	}

	return { trap_action, access_control };
}

uint32_t bus::calculate_physical_address(const int run_mode, const uint16_t a, const bool trap_on_failure, const bool is_write, const bool peek_only, const d_i_space_t space)
{
	uint32_t m_offset = a;

	if (mmu_->is_enabled() || (is_write && (mmu_->getMMR0() & (1 << 8 /* maintenance check */)))) {
		const uint8_t apf = a >> 13; // active page field

		bool          d   = space == d_space && mmu_->get_use_data_space(run_mode) ? space == d_space : false;

		uint16_t p_offset = a & 8191;  // page offset

		m_offset  = mmu_->get_physical_memory_offset(run_mode, d, apf);

		m_offset += p_offset;

		if ((mmu_->getMMR3() & 16) == 0)  // off is 18bit
			m_offset &= 0x3ffff;

		uint32_t io_base  = get_io_base();
		bool     is_io    = m_offset >= io_base;

		if (trap_on_failure) [[unlikely]] {
			{
				auto rc = get_trap_action(run_mode, d, apf, is_write);
				auto trap_action    = rc.first;
				int  access_control = rc.second;

				if (trap_action != T_PROCEED) {
					if (is_write)
						mmu_->set_page_trapped(run_mode, d, apf);

					if (mmu_->is_locked() == false) {
						uint16_t temp = mmu_->getMMR0();

						temp &= ~((1l << 15) | (1 << 14) | (1 << 13) | (1 << 12) | (3 << 5) | (7 << 1) | (1 << 4));

						if (is_write && access_control != 6)
							temp |= 1 << 13;  // read-only
								  //
						if (access_control == 0 || access_control == 4)
							temp |= 1l << 15;  // not resident
						else
							temp |= 1 << 13;  // read-only

						temp |= run_mode << 5;  // TODO: kernel-mode or user-mode when a trap occurs in user-mode?

						temp |= apf << 1; // add current page

						temp |= d << 4;

						mmu_->setMMR0(temp);

						DOLOG(debug, false, "MMR0: %06o", temp);
					}

					if (trap_action == T_TRAP_250) {
						DOLOG(debug, false, "Page access %d (for virtual address %06o): trap 0250", access_control, a);

						c->trap(0250);  // trap

						throw 5;
					}
					else {  // T_ABORT_4
						DOLOG(debug, false, "Page access %d (for virtual address %06o): trap 004", access_control, a);

						c->trap(004);  // abort

						throw 5;
					}
				}
			}

			if (m_offset >= n_pages * 8192l && !is_io) [[unlikely]] {
				DOLOG(debug, !peek_only, "bus::calculate_physical_address %o >= %o", m_offset, n_pages * 8192l);
				DOLOG(debug, false, "TRAP(04) (throw 6) on address %06o", a);

				if (mmu_->is_locked() == false) {
					uint16_t temp = mmu_->getMMR0();

					temp &= 017777;
					temp |= 1l << 15;  // non-resident

					temp &= ~14;  // add current page
					temp |= apf << 1;

					temp &= ~(3 << 5);
					temp |= run_mode << 5;

					mmu_->setMMR0(temp);
				}

				if (is_write)
					mmu_->set_page_trapped(run_mode, d, apf);

				c->trap(04);

				throw 6;
			}

			uint16_t pdr_len = mmu_->get_pdr_len(run_mode, d, apf);
			uint16_t pdr_cmp = (a >> 6) & 127;

			bool direction = mmu_->get_pdr_direction(run_mode, d, apf);

			// DOLOG(debug, false, "p_offset %06o pdr_len %06o direction %d, run_mode %d, apf %d, pdr: %06o", p_offset, pdr_len, direction, run_mode, apf, pages[run_mode][d][apf].pdr);

			if ((pdr_cmp > pdr_len && direction == false) || (pdr_cmp < pdr_len && direction == true)) {
				DOLOG(debug, false, "bus::calculate_physical_address::p_offset %o versus %o direction %d", pdr_cmp, pdr_len, direction);
				DOLOG(debug, false, "TRAP(0250) (throw 7) on address %06o", a);
				c->trap(0250);  // invalid access

				if (mmu_->is_locked() == false) {
					uint16_t temp = mmu_->getMMR0();

					temp &= 017777;
					temp |= 1 << 14;  // length

					temp &= ~14;  // add current page
					temp |= apf << 1;

					temp &= ~(3 << 5);
					temp |= run_mode << 5;

					temp &= ~(1 << 4);
					temp |= d << 4;

					mmu_->setMMR0(temp);
				}

				if (is_write)
					mmu_->set_page_trapped(run_mode, d, apf);

				throw 7;
			}
		}

		DOLOG(debug, false, "virtual address %06o maps to physical address %08o (run_mode: %d, apf: %d, par: %08o, poff: %o, AC: %d, %s)", a, m_offset, run_mode, apf,
				mmu_->get_physical_memory_offset(run_mode, d, apf),
				p_offset, mmu_->get_access_control(run_mode, d, apf), d ? "D" : "I");
	}
	else {
		// DOLOG(debug, false, "no MMU (read physical address %08o)", m_offset);
	}

	return m_offset;
}

write_rc_t bus::write(const uint16_t addr_in, const word_mode_t word_mode, uint16_t value, const rm_selection_t mode_selection, const d_i_space_t space)
{
	int           run_mode = mode_selection == rm_cur ? c->getPSW_runmode() : c->getPSW_prev_runmode();

	const uint8_t apf      = addr_in >> 13; // active page field

	bool          is_data  = space == d_space;

	bool          d        = is_data && mmu_->get_use_data_space(run_mode) ? is_data : false;

	if (mmu_->is_enabled() && (addr_in & 1) == 0 /* TODO remove this? */ && addr_in != ADDR_MMR0)
		mmu_->set_page_written_to(run_mode, d, apf);

	uint32_t m_offset = calculate_physical_address(run_mode, addr_in, true, true, false, space);

	uint32_t io_base  = get_io_base();
	bool     is_io    = m_offset >= io_base;

	if (is_io) {
		uint16_t a = m_offset - io_base + 0160000;  // TODO

		if (word_mode == wm_byte) {
			if (a == ADDR_PSW || a == ADDR_PSW + 1) { // PSW
				DOLOG(debug, false, "WRITE-I/O PSW %s: %03o", a & 1 ? "MSB" : "LSB", value);

				uint16_t vtemp = c->getPSW();

				update_word(&vtemp, a & 1, value);

				vtemp &= ~16;  // cannot set T bit via this

				c->setPSW(vtemp, false);

				return { true };
			}

			if (a == ADDR_STACKLIM || a == ADDR_STACKLIM + 1) { // stack limit register
				DOLOG(debug, false, "WRITE-I/O stack limit register %s: %03o", a & 1 ? "MSB" : "LSB", value);

				uint16_t v = c->getStackLimitRegister();

				update_word(&v, a & 1, value);

				v |= 0377;

				c->setStackLimitRegister(v);

				return { false };
			}

			if (a == ADDR_MICROPROG_BREAK_REG || a == ADDR_MICROPROG_BREAK_REG + 1) {  // microprogram break register
				DOLOG(debug, false, "WRITE-I/O micropram break register %s: %03o", a & 1 ? "MSB" : "LSB", value);

				update_word(&microprogram_break_register, a & 1, value);

				return { false };
			}

			if (a == ADDR_MMR0 || a == ADDR_MMR0 + 1) { // MMR0
				DOLOG(debug, false, "WRITE-I/O MMR0 register %s: %03o", a & 1 ? "MSB" : "LSB", value);

				uint16_t temp = mmu_->getMMR0();
				update_word(&temp, a & 1, value);
				mmu_->setMMR0(temp);

				return { false };
			}
		}
		else {
			if (a == ADDR_PSW) { // PSW
				DOLOG(debug, false, "WRITE-I/O PSW: %06o", value);
				c->setPSW(value & ~16, false);
				return { true };
			}

			if (a == ADDR_STACKLIM) { // stack limit register
				DOLOG(debug, false, "WRITE-I/O stack limit register: %06o", value);
				c->setStackLimitRegister(value & 0xff00);
				return { false };
			}

			if (a >= ADDR_KERNEL_R && a <= ADDR_KERNEL_R + 5) { // kernel R0-R5
				int reg = a - ADDR_KERNEL_R;
				DOLOG(debug, false, "WRITE-I/O kernel R%d: %06o", reg, value);
				c->setRegister(reg, value);
				return { false };
			}
			if (a >= ADDR_USER_R && a <= ADDR_USER_R + 5) { // user R0-R5
				int reg = a - ADDR_USER_R;
				DOLOG(debug, false, "WRITE-I/O user R%d: %06o", reg, value);
				c->setRegister(reg, value);
				return { false };
			}
			if (a == ADDR_KERNEL_SP) { // kernel SP
				DOLOG(debug, false, "WRITE-I/O kernel SP: %06o", value);
				c->setStackPointer(0, value);
				return { false };
			}
			if (a == ADDR_PC) { // PC
				DOLOG(debug, false, "WRITE-I/O PC: %06o", value);
				c->setPC(value);
				return { false };
			}
			if (a == ADDR_SV_SP) { // supervisor SP
				DOLOG(debug, false, "WRITE-I/O supervisor sp: %06o", value);
				c->setStackPointer(1, value);
				return { false };
			}
			if (a == ADDR_USER_SP) { // user SP
				DOLOG(debug, false, "WRITE-I/O user sp: %06o", value);
				c->setStackPointer(3, value);
				return { false };
			}

			if (a == ADDR_MICROPROG_BREAK_REG) {  // microprogram break register
				DOLOG(debug, false, "WRITE-I/O microprogram break register: %06o", value);
				microprogram_break_register = value & 0xff; // only 8b on 11/70?
				return { false };
			}
		}

		if (a == ADDR_CPU_ERR) { // cpu error register
			DOLOG(debug, false, "WRITE-I/O CPUERR: %06o", value);
			mmu_->setCPUERR(0);
			return { false };
		}

		if (a == ADDR_MMR3) { // MMR3
			DOLOG(debug, false, "WRITE-I/O set MMR3: %06o", value);
			mmu_->setMMR3(value);
			return { false };
		}

		if (a == ADDR_MMR0) { // MMR0
			DOLOG(debug, false, "WRITE-I/O set MMR0: %06o", value);
			mmu_->setMMR0(value);
			return { false };
		}

		if (a == ADDR_PIR) { // PIR
			DOLOG(debug, false, "WRITE-I/O set PIR: %06o", value);

			value &= 0177000;

			int bits = value >> 9;

			while(bits) {
				value += 042;  // bit 1...3 and 5...7
				bits >>= 1;
			}

			mmu_->setPIR(value);

			return { false };
		}

		if (a == ADDR_LFC) { // line frequency clock and status register
			kw11_l_->writeWord(a, value);

			return { false };
		}

		if (tm11 && a >= TM_11_BASE && a < TM_11_END) {
			DOLOG(debug, false, "WRITE-I/O TM11 register %d: %06o", (a - TM_11_BASE) / 2, value);
			word_mode == wm_byte ? tm11->writeByte(a, value) : tm11->writeWord(a, value);
			return { false };
		}

		if (rk05_ && a >= RK05_BASE && a < RK05_END) {
			DOLOG(debug, false, "WRITE-I/O RK05 register %d: %06o", (a - RK05_BASE) / 2, value);
			word_mode == wm_byte ? rk05_->writeByte(a, value) : rk05_->writeWord(a, value);
			return { false };
		}

		if (rl02_ && a >= RL02_BASE && a < RL02_END) {
			DOLOG(debug, false, "WRITE-I/O RL02 register %d: %06o", (a - RL02_BASE) / 2, value);
			word_mode == wm_byte ? rl02_->writeByte(a, value) : rl02_->writeWord(a, value);
			return { false };
		}

		if (tty_ && a >= PDP11TTY_BASE && a < PDP11TTY_END) {
			DOLOG(debug, false, "WRITE-I/O TTY register %d: %06o", (a - PDP11TTY_BASE) / 2, value);
			word_mode == wm_byte ? tty_->writeByte(a, value) : tty_->writeWord(a, value);
			return { false };
		}

		if (a >= 0172100 && a <= 0172137) {  // MM11-LP parity
			DOLOG(debug, false, "WRITE-I/O MM11-LP parity (%06o): %o", a, value);
			return { false };
		}

		/// MMU ///
		// supervisor
		if ((a >= ADDR_PDR_SV_START && a < ADDR_PDR_SV_END) ||
				(a >= ADDR_PAR_SV_START && a < ADDR_PAR_SV_END) ||
				(a >= ADDR_PDR_K_START && a < ADDR_PDR_K_END) ||
				(a >= ADDR_PAR_K_START && a < ADDR_PAR_K_END) ||
				(a >= ADDR_PDR_U_START && a < ADDR_PDR_U_END) ||
				(a >= ADDR_PAR_U_START && a < ADDR_PAR_U_END)) {
			if (word_mode == wm_word)
				mmu_->writeWord(a, value);
			else
				mmu_->writeByte(a, value);

			return { false };
		}

		if (a >= 0177740 && a <= 0177753) { // cache control register and others
			// TODO
			return { false };
		}

		if (a >= 0170200 && a <= 0170377) { // unibus map
			DOLOG(debug, false, "writing %06o to unibus map (%06o)", value, a);
			// TODO
			return { false };
		}

		if (a == ADDR_CONSW) {  // switch register
			console_leds = value;
			return { false };
		}

		if (a == ADDR_SYSSIZE || a == ADDR_SYSSIZE + 2)  // system size (is read-only)
			return { false };

		if (a == ADDR_SYSTEM_ID)  // is r/o
			return { false };

		///////////

		DOLOG(debug, false, "WRITE-I/O UNHANDLED %08o(%c): %06o (base: %o)", m_offset, word_mode == wm_byte ? 'B' : 'W', value, get_io_base());

		if (word_mode == wm_word && (a & 1)) {
			DOLOG(debug, false, "WRITE-I/O to %08o (value: %06o) - odd address!", m_offset, value);

			trap_odd(a);
			throw 8;
		}

		c->trap(004);  // no such i/o

		throw 9;
	}

	if (word_mode == wm_word && (addr_in & 1)) {
		DOLOG(debug, false, "WRITE to %06o (value: %06o) - odd address!", addr_in, value);

		trap_odd(addr_in);
		throw 10;
	}

	DOLOG(debug, false, "WRITE to %06o/%07o %c %c: %06o", addr_in, m_offset, space == d_space ? 'D' : 'I', word_mode == wm_byte ? 'B' : 'W', value);

	if (m_offset >= uint32_t(n_pages * 8192)) {
		c->trap(004);  // no such RAM
		throw 1;
	}

	if (word_mode == wm_byte)
		m->writeByte(m_offset, value);
	else
		m->writeWord(m_offset, value);

	return { false };
}

void bus::writePhysical(const uint32_t a, const uint16_t value)
{
	DOLOG(debug, false, "physicalWRITE %06o to %o", value, a);

	if (a >= n_pages * 8192l) {
		DOLOG(debug, false, "physicalWRITE to %o: trap 004", a);
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
		DOLOG(debug, false, "physicalREAD from %o: trap 004", a);
		c->trap(004);
		throw 13;
	}

	uint16_t value = m->readWord(a);

	DOLOG(debug, false, "physicalREAD %06o from %o", value, a);

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

uint8_t bus::readUnibusByte(const uint32_t a)
{
	uint8_t v = m->readByte(a);
	DOLOG(debug, false, "readUnibusByte[%08o]=%03o", a, v);
	return v;
}

void bus::writeUnibusByte(const uint32_t a, const uint8_t v)
{
	DOLOG(debug, false, "writeUnibusByte[%08o]=%03o", a, v);
	m->writeByte(a, v);
}
