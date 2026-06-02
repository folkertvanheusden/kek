// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <ArduinoJson.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "deqna.h"
#include "dz11.h"
#include "kw11-l.h"
#include "log.h"
#include "memory.h"
#include "mmu.h"
#if !defined(TEENSY4_1)
#include "tm-11.h"
#endif
#include "tty.h"
#include "utils.h"

#if defined(ESP32)
#include <esp_debug_helpers.h>
#endif


bus::bus()
{
	mmu_    = new mmu();
	kw11_l_ = new kw11_l(this);

	reset(true);
}

bus::~bus()
{
	delete kw11_l_;
	delete c;
#if !defined(TEENSY4_1)
	delete tm11;
#endif
	delete rk05_;
	delete rl02_;
	delete tty_;
	delete mmu_;
	delete dz11_;
	delete rp06_;
	delete deqna_;
	delete m;
}

JsonDocument bus::serialize() const
{
	JsonDocument j_out;

	if (m)
		j_out["memory"] = m->serialize();

	if (kw11_l_)
		j_out["kw11-l"] = kw11_l_->serialize();

	if (tty_)
		j_out["tty"]    = tty_->serialize();

	if (mmu_)
		j_out["mmu"]    = mmu_->serialize();

	if (c)
		j_out["cpu"]    = c->serialize();

	if (rl02_)
		j_out["rl02"]   = rl02_->serialize();

	if (rk05_)
		j_out["rk05"]   = rk05_->serialize();

	if (dz11_)
		j_out["dz11"]   = dz11_->serialize();

	if (rp06_)
		j_out["rp06"]   = rp06_->serialize();

	// TODO: tm11

	// TODO: deqna
	return j_out;
}

bus *bus::deserialize(const JsonDocument j, console *const cnsl, kek_event_t *const event)
{
	bus *b = new bus();

	memory *m = nullptr;
	if (j.containsKey("memory")) {
		m = memory::deserialize(j["memory"]);
		b->add_ram(m);
	}

	if (j.containsKey("tty"))
		b->add_tty(tty::deserialize(j["tty"], b, cnsl));

	cpu *c = nullptr;
	if (j.containsKey("cpu")) {
		c = cpu::deserialize(j["cpu"], b, event);
		b->add_cpu(c);
	}

	if (j.containsKey("mmu"))
		b->add_mmu(mmu::deserialize(j["mmu"], m, c));

	if (j.containsKey("rl02"))
		b->add_rl02(rl02::deserialize(j["rl02"], b));

	if (j.containsKey("rk05"))
		b->add_rk05(rk05::deserialize(j["rk05"], b));

	if (j.containsKey("kw11-l"))
		b->add_KW11_L(kw11_l::deserialize(j["kw11-l"], b, cnsl));

	if (j.containsKey("dz11"))
		b->add_DZ11(dz11::deserialize(j["dz11"], b));

	if (j.containsKey("rp06"))
		b->add_RP06(rp06::deserialize(j["rp06"], b));

	// TODO: tm11

	// TODO: deqna

	return b;
}

void bus::show_state(console *const cnsl) const
{
	cnsl->put_string_lf(format("Microprogram break register: %06o", microprogram_break_register));
	cnsl->put_string_lf(format("Console switches: %06o", console_switches));
	cnsl->put_string_lf(format("Console LEDs: %06o", console_leds));
}

void bus::set_memory_size(const int n_pages)
{
	uint32_t n_bytes = n_pages * 8192l;

	delete m;
	m = new memory(n_bytes);

	mmu_->begin(m, c);

	DOLOG(log_ss::LS_BUS, "Memory is now %u kB (%d pages)", n_bytes / 1024, n_pages);
}

void bus::init()
{
	if (m)
		m->reset(true);
	if (c)
		c->reset();
	reset(true);
}

void bus::reset(const bool hard)
{
	if (mmu_)
		mmu_->reset(hard);
#if !defined(TEENSY4_1)
	if (tm11)
		tm11->reset(hard);
#endif
	if (rk05_)
		rk05_->reset(hard);
	if (rl02_)
		rl02_->reset(hard);
	if (tty_)
		tty_->reset(hard);
	if (kw11_l_)
		kw11_l_->reset(hard);
	if (dz11_)
		dz11_->reset(hard);
	if (dc11_)
		dc11_->reset(hard);
	if (rp06_)
		rp06_->reset(hard);
	if (deqna_)
		deqna_->reset(hard);

	mmu_->setMMR0(0);
	mmu_->setMMR3(0);
}

void bus::add_DEQNA(deqna *const deqna_)
{
	delete this->deqna_;
	this->deqna_ = deqna_;
}

void bus::add_RP06(rp06 *const rp06_)
{
	delete this->rp06_;
	this->rp06_ = rp06_;
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

	mmu_->begin(m, c);
}

void bus::add_mmu(mmu *const mmu_)
{
	delete this->mmu_;
	this->mmu_ = mmu_;

	mmu_->begin(m, c);
}

void bus::add_cpu(cpu *const c)
{
	delete this->c;
	this->c = c;

	if (mmu_)
		mmu_->begin(m, c);
}

#if !defined(TEENSY4_1)
void bus::add_tm11(tm_11 *const tm11)
{
	delete this->tm11;
	this->tm11= tm11;
} 
#endif

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
	this->tty_ = tty_;
}

void bus::add_DZ11(dz11 *const dz11_)
{
	delete this->dz11_;
	this->dz11_ = dz11_;
}

void bus::add_DC11(dc11 *const dc11_)
{
	delete this->dc11_;
	this->dc11_ = dc11_;
}

void bus::del_DZ11()
{
	delete dz11_;
	dz11_ = nullptr;
}

uint16_t bus::read_IO(const uint16_t a, const word_mode_t word_mode, const int run_mode, const d_i_space_t space, const int page)
{
	//// REGISTERS ////
	if (a >= ADDR_KERNEL_R && a <= ADDR_KERNEL_R + 5) { // kernel R0-R5
		uint16_t temp = c->get_register(a - ADDR_KERNEL_R) & (word_mode == wm_byte ? 0xff : 0xffff);
		DOLOG(log_ss::LS_BUS, "READ-I/O kernel R%d: %06o", a - ADDR_KERNEL_R, temp);
		return temp;
	}
	if (a >= ADDR_USER_R && a <= ADDR_USER_R + 5) { // user R0-R5
		uint16_t temp = c->get_register(a - ADDR_USER_R) & (word_mode == wm_byte ? 0xff : 0xffff);
		DOLOG(log_ss::LS_BUS, "READ-I/O user R%d: %06o", a - ADDR_USER_R, temp);
		return temp;
	}
	if (a == ADDR_KERNEL_SP) { // kernel SP
		uint16_t temp = c->get_stackpointer(0) & (word_mode == wm_byte ? 0xff : 0xffff);
		DOLOG(log_ss::LS_BUS, "READ-I/O kernel SP: %06o", temp);
		return temp;
	}
	if (a == ADDR_PC) { // PC
		uint16_t temp = c->getPC() & (word_mode == wm_byte ? 0xff : 0xffff);
		DOLOG(log_ss::LS_BUS, "READ-I/O PC: %06o", temp);
		return temp;
	}
	if (a == ADDR_SV_SP) { // supervisor SP
		uint16_t temp = c->get_stackpointer(1) & (word_mode == wm_byte ? 0xff : 0xffff);
		DOLOG(log_ss::LS_BUS, "READ-I/O supervisor SP: %06o", temp);
		return temp;
	}
	if (a == ADDR_USER_SP) { // user SP
		uint16_t temp = c->get_stackpointer(3) & (word_mode == wm_byte ? 0xff : 0xffff);
		DOLOG(log_ss::LS_BUS, "READ-I/O user SP: %06o", temp);
		return temp;
	}
	///^ registers ^///

	if ((a & 1) && word_mode == wm_word) [[unlikely]] {
		DOLOG(log_ss::LS_BUS, "READ-I/O odd address %06o UNHANDLED", a);
		int page_index = mmu_->calc_par_pdr_index(run_mode, space, page);
		mmu_->trap_if_odd(page_index);
		throw 0;
		return 0;
	}

	if (a == ADDR_CPU_ERR) { // cpu error register
		uint16_t temp = mmu_->getCPUERR() & 0xff;
		DOLOG(log_ss::LS_BUS, "READ-I/O CPU error: %03o", temp);
		return temp;
	}

	if (a == ADDR_MAINT) { // MAINT
		uint16_t temp = 1; // POWER OK
		DOLOG(log_ss::LS_BUS, "READ-I/O MAINT: %o", temp);
		return temp;
	}

	if (a == ADDR_CONSW) { // console switch & display register
		uint16_t temp = console_switches;
		DOLOG(log_ss::LS_BUS, "READ-I/O console switch: %o", temp);
		return temp;
	}

	if (a == ADDR_PIR || a == ADDR_PIR + 1) { // PIR
		uint16_t temp = 0;

		uint16_t PIR  = mmu_->getPIR();

		if (word_mode == wm_word)
			temp = PIR;
		else
			temp = a == ADDR_PIR ? PIR & 255 : PIR >> 8;

		DOLOG(log_ss::LS_BUS, "READ-I/O PIR: %o", temp);
		return temp;
	}

	if (a == ADDR_SYSTEM_ID) {
		uint16_t temp = 011064;
		DOLOG(log_ss::LS_BUS, "READ-I/O system id: %o", temp);
		return temp;
	}

	if (a == ADDR_LFC) // line frequency clock and status register
		return kw11_l_->read_word(a);

	if (a == ADDR_LP11CSR) { // printer, CSR register, LP11
		uint16_t temp = 0x80;
		DOLOG(log_ss::LS_BUS, "READ-I/O LP11 CSR: %o", temp);
		return temp;
	}

	/// MMU ///
	if ((a >= ADDR_PDR_SV_START && a < ADDR_PAR_SV_END) ||
			(a >= ADDR_PDR_K_START && a < ADDR_PAR_K_END) ||
			(a >= ADDR_PDR_U_START && a < ADDR_PAR_U_END)) {
		if (word_mode == wm_word)
			return mmu_->read_word(a);

		return mmu_->read_byte(a);
	}
	///////////

	if (a >= 0177740 && a <= 0177753) { // cache control register and others
		DOLOG(log_ss::LS_BUS, "READ-I/O cache control register/others (%06o): %o", a, 0);
		// TODO
		return 0;
	}

	if (a >= 0170200 && a <= 0170377) { // unibus map
		DOLOG(log_ss::LS_BUS, "READ-I/O unibus map (%06o): %o", a, 0);
		// TODO
		return 0;
	}

	if (a >= 0172100 && a <= 0172137) {  // MM11-LP parity
		DOLOG(log_ss::LS_BUS, "READ-I/O MM11-LP parity (%06o): %o", a, 1);
		return 1;
	}

	if (word_mode == wm_byte) {
		if (a == ADDR_PSW) { // PSW
			uint8_t temp = c->getPSW();
			DOLOG(log_ss::LS_BUS, "READ-I/O PSW LSB: %03o", temp);
			return temp;
		}

		if (a == ADDR_PSW + 1) {
			uint8_t temp = c->getPSW() >> 8;
			DOLOG(log_ss::LS_BUS, "READ-I/O PSW MSB: %03o", temp);
			return temp;
		}
		if (a == ADDR_STACKLIM) { // stack limit register
			uint8_t temp = c->get_stack_limit_register();
			DOLOG(log_ss::LS_BUS, "READ-I/O stack limit register (low): %03o", temp);
			return temp;
		}
		if (a == ADDR_STACKLIM + 1) { // stack limit register
			uint8_t temp = c->get_stack_limit_register() >> 8;
			DOLOG(log_ss::LS_BUS, "READ-I/O stack limit register (high): %03o", temp);
			return temp;
		}

		if (a == ADDR_MICROPROG_BREAK_REG) {  // microprogram break register
			uint8_t temp = microprogram_break_register;
			DOLOG(log_ss::LS_BUS, "READ-I/O microprogram break register (low): %03o", temp);
			return temp;
		}
		if (a == ADDR_MICROPROG_BREAK_REG + 1) {  // microprogram break register
			uint8_t temp = microprogram_break_register >> 8;
			DOLOG(log_ss::LS_BUS, "READ-I/O microprogram break register (high): %03o", temp);
			return temp;
		}

		if (a == ADDR_MMR0) {
			uint8_t temp = mmu_->getMMR0();
			DOLOG(log_ss::LS_BUS, "READ-I/O MMR0 LO: %03o", temp);
			return temp;
		}
		if (a == ADDR_MMR0 + 1) {
			uint8_t temp = mmu_->getMMR0() >> 8;
			DOLOG(log_ss::LS_BUS, "READ-I/O MMR0 HI: %03o", temp);
			return temp;
		}
	}
	else {
		if (a == ADDR_MMR0) {
			uint16_t temp = mmu_->getMMR0();
			DOLOG(log_ss::LS_BUS, "READ-I/O MMR0: %06o", temp);
			return temp;
		}

		if (a == ADDR_MMR1) { // MMR1
			uint16_t temp = mmu_->getMMR1();
			DOLOG(log_ss::LS_BUS, "READ-I/O MMR1: %06o", temp);
			return temp;
		}

		if (a == ADDR_MMR2) { // MMR2
			uint16_t temp = mmu_->getMMR2();
			DOLOG(log_ss::LS_BUS, "READ-I/O MMR2: %06o", temp);
			return temp;
		}

		if (a == ADDR_MMR3) { // MMR3
			uint16_t temp = mmu_->getMMR3();
			DOLOG(log_ss::LS_BUS, "READ-I/O MMR3: %06o", temp);
			return temp;
		}

		if (a == ADDR_PSW) { // PSW
			uint16_t temp = c->getPSW();
			DOLOG(log_ss::LS_BUS, "READ-I/O PSW: %06o", temp);
			return temp;
		}

		if (a == ADDR_STACKLIM) { // stack limit register
			uint16_t temp = c->get_stack_limit_register();
			DOLOG(log_ss::LS_BUS, "READ-I/O stack limit register: %06o", temp);
			return temp;
		}

		if (a == ADDR_CPU_ERR) { // cpu error register
			uint16_t temp = mmu_->getCPUERR();
			DOLOG(log_ss::LS_BUS, "READ-I/O CPUERR: %06o", temp);
			return temp;
		}

		if (a == ADDR_MICROPROG_BREAK_REG) {  // microprogram break register
			uint16_t temp = microprogram_break_register;
			DOLOG(log_ss::LS_BUS, "READ-I/O microprogram break register: %06o", temp);
			return temp;
		}
	}

#if !defined(TEENSY4_1)
	if (tm11 && a >= TM_11_BASE && a < TM_11_END)
		return word_mode == wm_byte ? tm11->read_byte(a) : tm11->read_word(a);
#endif

	if (rk05_ && a >= RK05_BASE && a < RK05_END)
		return word_mode == wm_byte ? rk05_->read_byte(a) : rk05_->read_word(a);

	if (rl02_ && a >= RL02_BASE && a < RL02_END)
		return word_mode == wm_byte ? rl02_->read_byte(a) : rl02_->read_word(a);

	if (tty_ && a >= PDP11TTY_BASE && a < PDP11TTY_END)
		return word_mode == wm_byte ? tty_->read_byte(a) : tty_->read_word(a);

	if (dc11_ && a >= DC11_BASE && a < DC11_END)
		return word_mode == wm_byte ? dc11_->read_byte(a) : dc11_->read_word(a);

	if (dz11_ && a >= DZ11_BASE && a < DZ11_END)
		return word_mode == wm_byte ? dz11_->read_byte(a) : dz11_->read_word(a);

	if (rp06_ && a >= RP06_BASE && a < RP06_END)
		return word_mode == wm_byte ? rp06_->read_byte(a) : rp06_->read_word(a);

	if (deqna_ && a >= DEQNA_BASE && a < DEQNA_END)
		return word_mode == wm_byte ? deqna_->read_byte(a) : deqna_->read_word(a);

	// LO size register field must be all 1s, so subtract 1
	uint32_t system_size = m->get_memory_size() / 64 - 1;
	if (system_size == 0177777)
		system_size = 0167777;

	if (a == ADDR_SYSSIZE + 2) {  // system size HI
		uint16_t temp = system_size >> 16;
		DOLOG(log_ss::LS_BUS, "READ-I/O accessing system size HI: %06o", temp);
		return temp;
	}

	if (a == ADDR_SYSSIZE) {  // system size LO
		uint16_t temp = system_size;
		DOLOG(log_ss::LS_BUS, "READ-I/O accessing system size LO: %06o", temp);
		return temp;
	}

	DOLOG(log_ss::LS_BUS, "READ-I/O UNHANDLED read %06o (%c), (base: %o)", a + mmu_->get_io_base(), word_mode == wm_byte ? 'B' : ' ', mmu_->get_io_base());

	c->trap(004);  // no such i/o
	throw 1;

	return 0;
}

void bus::verify_pointer_bounds(const uint32_t m_offset, const int page_index)
{
	if (m_offset >= m->get_memory_size()) [[unlikely]] {
		DOLOG(log_ss::LS_BUS, "TRAP(04) (throw 6) on address %08o", m_offset);

		if (mmu_->is_locked() == false) {
			uint16_t temp = mmu_->getMMR0();

			temp &= 017777;
			temp |= 1l << 15;  // non-resident

			const auto [ run_mode, d, apf ] = mmu_->explode_page_index(page_index);

			temp &= ~14;  // add current page
			temp |= apf << 1;

			temp &= ~(3 << 5);
			temp |= run_mode << 5;

			mmu_->setMMR0_as_is(temp);
		}

		DOLOG(log_ss::LS_BUS, "TRAP 250 for access valid");
		c->trap(0250);

		throw 6;
	}
}

uint16_t bus::read(const uint16_t addr_in, const word_mode_t word_mode, const int run_mode, const d_i_space_t space_in)
{
	auto     space    = mmu_->get_use_data_space(run_mode) ? space_in : i_space;
	uint32_t m_offset = mmu_->calculate_physical_address(run_mode, addr_in, false, space);

	uint32_t io_base  = mmu_->get_io_base();
	bool     is_io    = m_offset >= io_base;
	int      apf      = addr_in >> 13;

	if (is_io) {
		uint16_t a = m_offset - io_base + 0160000;  // TODO
		return read_IO(a, word_mode, run_mode, space, apf);
	}

	int page_index = mmu_->calc_par_pdr_index(run_mode, space, apf);

	verify_pointer_bounds(m_offset, page_index);

	mmu_->set_page_accessed(page_index);

	uint16_t temp = 0;
	if (word_mode == wm_byte)
		temp = m->read_byte(m_offset);
	else {
		if (m_offset & 1) {
			DOLOG(log_ss::LS_BUS, "READ from %08o - odd address!", m_offset);
			mmu_->trap_if_odd(page_index);
			throw 2;
		}

		temp = m->read_word(m_offset);
	}

	DOLOG(log_ss::LS_BUS, "READ from %06o/%07o %c %c: %06o (%d)", addr_in, m_offset, space == d_space ? 'D' : 'I', word_mode == wm_byte ? 'B' : 'W', temp, run_mode);

	return temp;
}

bool bus::write_IO(const uint16_t a, const word_mode_t word_mode, const int page, uint16_t value)
{
	if (word_mode == wm_byte) {
		if (a == ADDR_PSW || a == ADDR_PSW + 1) { // PSW
			DOLOG(log_ss::LS_BUS, "WRITE-I/O PSW %s: %03o", a & 1 ? "MSB" : "LSB", value);

			uint16_t vtemp = c->getPSW();
			update_word(&vtemp, a & 1, value);
			vtemp &= ~16;  // cannot set T bit via this
			c->setPSW(vtemp, false);

			return true;
		}

		if (a == ADDR_STACKLIM || a == ADDR_STACKLIM + 1) { // stack limit register
			DOLOG(log_ss::LS_BUS, "WRITE-I/O stack limit register %s: %03o", a & 1 ? "MSB" : "LSB", value);

			uint16_t v = c->get_stack_limit_register();
			update_word(&v, a & 1, value);
			v &= 0xff00;
			c->set_stack_limit_register(v);

			return false;
		}

		if (a == ADDR_MICROPROG_BREAK_REG || a == ADDR_MICROPROG_BREAK_REG + 1) {  // microprogram break register
			DOLOG(log_ss::LS_BUS, "WRITE-I/O micropram break register %s: %03o", a & 1 ? "MSB" : "LSB", value);

			update_word(&microprogram_break_register, a & 1, value);

			return false;
		}

		if (a == ADDR_MMR0 || a == ADDR_MMR0 + 1) { // MMR0
			DOLOG(log_ss::LS_BUS, "WRITE-I/O MMR0 register %s: %03o", a & 1 ? "MSB" : "LSB", value);

			uint16_t temp = mmu_->getMMR0();
			update_word(&temp, a & 1, value);
			mmu_->setMMR0(temp);

			return false;
		}
	}
	else {
		if (a == ADDR_PSW) { // PSW
			DOLOG(log_ss::LS_BUS, "WRITE-I/O PSW: %06o", value);
			c->setPSW(value & ~16, false);
			return { true };
		}

		if (a == ADDR_STACKLIM) { // stack limit register
			DOLOG(log_ss::LS_BUS, "WRITE-I/O stack limit register: %06o", value);
			c->set_stack_limit_register(value & 0xff00);
			return false;
		}

		if (a >= ADDR_KERNEL_R && a <= ADDR_KERNEL_R + 5) { // kernel R0-R5
			int reg = a - ADDR_KERNEL_R;
			DOLOG(log_ss::LS_BUS, "WRITE-I/O kernel R%d: %06o", reg, value);
			c->set_register(reg, value);
			return false;
		}
		if (a >= ADDR_USER_R && a <= ADDR_USER_R + 5) { // user R0-R5
			int reg = a - ADDR_USER_R;
			DOLOG(log_ss::LS_BUS, "WRITE-I/O user R%d: %06o", reg, value);
			c->set_register(reg, value);
			return false;
		}
		if (a == ADDR_KERNEL_SP) { // kernel SP
			DOLOG(log_ss::LS_BUS, "WRITE-I/O kernel SP: %06o", value);
			c->set_stackpointer(0, value);
			return false;
		}
		if (a == ADDR_PC) { // PC
			DOLOG(log_ss::LS_BUS, "WRITE-I/O PC: %06o", value);
			c->setPC(value);
			return false;
		}
		if (a == ADDR_SV_SP) { // supervisor SP
			DOLOG(log_ss::LS_BUS, "WRITE-I/O supervisor sp: %06o", value);
			c->set_stackpointer(1, value);
			return false;
		}
		if (a == ADDR_USER_SP) { // user SP
			DOLOG(log_ss::LS_BUS, "WRITE-I/O user sp: %06o", value);
			c->set_stackpointer(3, value);
			return false;
		}

		if (a == ADDR_MICROPROG_BREAK_REG) {  // microprogram break register
			DOLOG(log_ss::LS_BUS, "WRITE-I/O microprogram break register: %06o", value);
			microprogram_break_register = value & 0xff; // only 8b on 11/70?
			return false;
		}
	}

	if (a == ADDR_CPU_ERR) { // cpu error register
		DOLOG(log_ss::LS_BUS, "WRITE-I/O CPUERR: %06o", value);
		mmu_->setCPUERR(0);
		return false;
	}

	if (a == ADDR_MMR3) { // MMR3
		DOLOG(log_ss::LS_BUS, "WRITE-I/O set MMR3: %06o", value);
		mmu_->setMMR3(value);
		return false;
	}

	if (a == ADDR_MMR0) { // MMR0
		DOLOG(log_ss::LS_BUS, "WRITE-I/O set MMR0: %06o", value);
		mmu_->setMMR0(value);
		return false;
	}

	if (a == ADDR_PIR) { // PIR
		DOLOG(log_ss::LS_BUS, "WRITE-I/O set PIR: %06o", value);

		value &= 0177000;

		int bits = value >> 9;

		while(bits) {
			value += 042;  // bit 1...3 and 5...7
			bits >>= 1;
		}

		mmu_->setPIR(value);

		return false;
	}

	if (a == ADDR_LFC) { // line frequency clock and status register
		kw11_l_->write_word(a, value);

		return false;
	}

#if !defined(TEENSY4_1)
	if (tm11 && a >= TM_11_BASE && a < TM_11_END) {
		DOLOG(log_ss::LS_BUS, "WRITE-I/O TM11 register %d: %06o", (a - TM_11_BASE) / 2, value);
		word_mode == wm_byte ? tm11->write_byte(a, value) : tm11->write_word(a, value);
		return false;
	}
#endif

	if (rk05_ && a >= RK05_BASE && a < RK05_END) {
		DOLOG(log_ss::LS_BUS, "WRITE-I/O RK05 register %d: %06o", (a - RK05_BASE) / 2, value);
		word_mode == wm_byte ? rk05_->write_byte(a, value) : rk05_->write_word(a, value);
		return false;
	}

	if (rl02_ && a >= RL02_BASE && a < RL02_END) {
		DOLOG(log_ss::LS_BUS, "WRITE-I/O RL02 register %d: %06o", (a - RL02_BASE) / 2, value);
		word_mode == wm_byte ? rl02_->write_byte(a, value) : rl02_->write_word(a, value);
		return false;
	}

	if (tty_ && a >= PDP11TTY_BASE && a < PDP11TTY_END) {
		DOLOG(log_ss::LS_BUS, "WRITE-I/O TTY register %d: %06o", (a - PDP11TTY_BASE) / 2, value);
		word_mode == wm_byte ? tty_->write_byte(a, value) : tty_->write_word(a, value);
		return false;
	}

	if (dc11_ && a >= DC11_BASE && a < DC11_END) {
		word_mode == wm_byte ? dc11_->write_byte(a, value) : dc11_->write_word(a, value);
		return false;
	}

	if (dz11_ && a >= DZ11_BASE && a < DZ11_END) {
		word_mode == wm_byte ? dz11_->write_byte(a, value) : dz11_->write_word(a, value);
		return false;
	}

	if (rp06_ && a >= RP06_BASE && a < RP06_END) {
		word_mode == wm_byte ? rp06_->write_byte(a, value) : rp06_->write_word(a, value);
		return false;
	}

	if (deqna_ && a >= DEQNA_BASE && a < DEQNA_END) {
		word_mode == wm_byte ? deqna_->write_byte(a, value) : deqna_->write_word(a, value);
		return false;
	}

	if (a >= 0172100 && a <= 0172137) {  // MM11-LP parity
		DOLOG(log_ss::LS_BUS, "WRITE-I/O MM11-LP parity (%06o): %o", a, value);
		return false;
	}

	/// MMU ///
	if ((a >= ADDR_PDR_SV_START && a < ADDR_PAR_SV_END) ||
			(a >= ADDR_PDR_K_START && a < ADDR_PAR_K_END) ||
			(a >= ADDR_PDR_U_START && a < ADDR_PAR_U_END)) {
		if (word_mode == wm_word)
			mmu_->write_word(a, value);
		else
			mmu_->write_byte(a, value);

		return false;
	}
	///////////

	if (a >= 0177740 && a <= 0177753) { // cache control register and others
					    // TODO
		return false;
	}

	if (a >= 0170200 && a <= 0170377) { // unibus map
		DOLOG(log_ss::LS_BUS, "writing %06o to unibus map (%06o)", value, a);
		// TODO
		return false;
	}

	if (a == ADDR_CONSW) {  // switch register
		console_leds = value;
		return false;
	}

	if (a == ADDR_SYSSIZE || a == ADDR_SYSSIZE + 2)  // system size (is read-only)
		return false;

	if (a == ADDR_SYSTEM_ID)  // is r/o
		return false;

	///////////

	DOLOG(log_ss::LS_BUS, "WRITE-I/O UNHANDLED %08o(%c): %06o (base: %o)", a + mmu_->get_io_base(), word_mode == wm_byte ? 'B' : 'W', value, mmu_->get_io_base());

	if (word_mode == wm_word && (a & 1)) [[unlikely]] {
		DOLOG(log_ss::LS_BUS, "WRITE-I/O to %08o (value: %06o) - odd address!", a + mmu_->get_io_base(), value);

		mmu_->trap_if_odd(page);

		throw 8;
	}

	c->trap(004);  // no such i/o

	throw 9;
}

bool bus::write(const uint16_t addr_in, const word_mode_t word_mode, const uint16_t value, const int run_mode, const d_i_space_t space_in)
{
	const uint8_t apf        = addr_in >> 13; // active page field
	auto          space      = mmu_->get_use_data_space(run_mode) ? space_in : i_space;
	int           page_index = mmu_->calc_par_pdr_index(run_mode, space, apf);
	uint32_t      m_offset   = mmu_->calculate_physical_address(run_mode, addr_in, true, space);

	uint32_t      io_base    = mmu_->get_io_base();
	bool          is_io      = m_offset >= io_base;

	if (is_io) {
		uint16_t a = m_offset - io_base + 0160000;  // TODO
		return write_IO(a, word_mode, apf, value);
	}

	DOLOG(log_ss::LS_BUS, "WRITE to %06o/%07o %c %c: %06o", addr_in, m_offset, space == d_space ? 'D' : 'I', word_mode == wm_byte ? 'B' : 'W', value);

	verify_pointer_bounds(m_offset, page_index);

	mmu_->set_page_written_to(page_index);

	if (word_mode == wm_byte)
		m->write_byte(m_offset, value);
	else {
		if (m_offset & 1) [[unlikely]] {
			DOLOG(log_ss::LS_BUS, "WRITE to %08o (value: %06o) - odd address!", m_offset, value);
			mmu_->trap_if_odd(page_index);
			throw 10;
		}

		m->write_word(m_offset, value);
	}

	return false;
}

void bus::write_unibus_word(const uint32_t a, const uint16_t v)
{
	DOLOG(log_ss::LS_BUS, "write_unibus_word[%08o]=%06o (%04x)", a, v, v);
	if (a < m->get_memory_size())
		m->write_word(a, v);
}

void bus::write_physical(const uint32_t a, const uint16_t value)
{
	DOLOG(log_ss::LS_BUS, "physicalWRITE %06o to %o", value, a);

	if (a >= m->get_memory_size()) {
		DOLOG(log_ss::LS_BUS, "physicalWRITE to %o: trap 004", a);
		c->trap(004);
		throw 12;
	}
	else {
		m->write_word(a, value);
	}
}

uint16_t bus::read_physical(const uint32_t a)
{
	if (a >= m->get_memory_size()) {
		DOLOG(log_ss::LS_BUS, "read_physical from %o: trap 004", a);
		c->trap(004);
		throw 13;
	}

	uint16_t value = m->read_word(a);
	DOLOG(log_ss::LS_BUS, "read_physical %06o from %o", value, a);
	return value;
}

uint16_t bus::read_physical_byte(const uint32_t a)
{
	if (a >= m->get_memory_size()) {
		DOLOG(log_ss::LS_BUS, "read_physical_byte from %o: trap 004", a);
		c->trap(004);
		throw 13;
	}

	uint16_t value = m->read_byte(a);
	DOLOG(log_ss::LS_BUS, "read_physical_byte %03o from %o", value, a);
	return value;
}

uint16_t bus::read_word(const uint16_t a, const d_i_space_t s)
{
	return read(a, wm_word, c->getPSW_runmode(), s);
}

std::optional<uint16_t> bus::peek_word(const int run_mode, const uint16_t a)
{
	auto meta = mmu_->calculate_physical_address(run_mode, a);

	uint32_t io_base  = mmu_->get_io_base();
	if (meta.physical_instruction >= io_base)
		return { };

	if (meta.physical_instruction >= m->get_memory_size())
		return { };

	return m->read_word(meta.physical_instruction);
}

void bus::write_word(const uint16_t a, const uint16_t value, const d_i_space_t s)
{
	write(a, wm_word, value, c->getPSW_runmode(), s);
}

// TODO check for odd address
uint16_t bus::read_unibus_word(const uint32_t a) const
{
	uint16_t v = 0;
	if (a < m->get_memory_size())
		v = m->read_word(a);
	DOLOG(log_ss::LS_BUS, "read_unibus_word[%08o]=%06o (0x%04x)", a, v, v);
	return v;
}

uint8_t bus::read_unibus_byte(const uint32_t a) const
{
	uint8_t v = 0;
	if (a < m->get_memory_size())
		v = m->read_byte(a);
	DOLOG(log_ss::LS_BUS, "read_unibus_byte[%08o]=%03o (0x%02x)", a, v, v);
	return v;
}

void bus::write_unibus_byte(const uint32_t a, const uint8_t v)
{
	DOLOG(log_ss::LS_BUS, "write_unibus_byte[%08o]=%03o (0x%02x)", a, v, v);
	if (a < m->get_memory_size())
		m->write_byte(a, v);
}
