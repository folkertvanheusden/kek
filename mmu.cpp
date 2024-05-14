#include <cassert>
#include <cstring>

#include "bus.h"  // for (at least) ADDR_PSW
#include "gen.h"
#include "log.h"
#include "mmu.h"
#include "utils.h"


mmu::mmu()
{
}

mmu::~mmu()
{
}

void mmu::begin(memory *const m)
{
	this->m = m;

	reset();
}

void mmu::reset()
{
	memset(pages, 0x00, sizeof pages);

	CPUERR = MMR0 = MMR1 = MMR2 = MMR3 = PIR = CSR = 0;
}

void mmu::dump_par_pdr(console *const cnsl, const int run_mode, const bool d, const std::string & name, const int state, const std::optional<int> & selection) const
{
	if (state == 0 || state == 2)
		cnsl->put_string_lf(name);
	else
		cnsl->put_string_lf(format("%s DISABLED", name.c_str()));

	cnsl->put_string_lf("   PAR             PDR    LEN");

	for(int i=0; i<8; i++) {
		if (selection.has_value() && i != selection.value())
			continue;
		uint16_t par_value = pages[run_mode][d][i].par;
		uint16_t pdr_value = pages[run_mode][d][i].pdr;

		uint16_t pdr_len   = (((pdr_value >> 8) & 127) + 1) * 64;

		cnsl->put_string_lf(format("%d] %06o %08o %06o %04o D%d A%d", i, par_value, par_value * 64, pdr_value, pdr_len, !!(pdr_value & 8), pdr_value & 7));
	}
}

void mmu::show_state(console *const cnsl) const
{
	cnsl->put_string_lf(MMR0 & 1 ? "MMU enabled" : "MMU NOT enabled");

	cnsl->put_string_lf(format("MMR0: %06o", MMR0));
	cnsl->put_string_lf(format("MMR1: %06o", MMR1));
	cnsl->put_string_lf(format("MMR2: %06o", MMR2));
	cnsl->put_string_lf(format("MMR3: %06o", MMR3));

	dump_par_pdr(cnsl, 1, false, "supervisor i-space", 0,                  { });
	dump_par_pdr(cnsl, 1, true,  "supervisor d-space", 1 + (!!(MMR3 & 2)), { });

	dump_par_pdr(cnsl, 0, false, "kernel i-space",     0,                  { });
	dump_par_pdr(cnsl, 0, true,  "kernel d-space",     1 + (!!(MMR3 & 2)), { });

	dump_par_pdr(cnsl, 3, false, "user i-space",       0,                  { });
	dump_par_pdr(cnsl, 3, true,  "user d-space",       1 + (!!(MMR3 & 2)), { });
}

uint16_t mmu::read_pdr(const uint32_t a, const int run_mode)
{
	int      page = (a >> 1) & 7;
	bool     is_d = a & 16;
	uint16_t t    = pages[run_mode][is_d][page].pdr;

	return t;
}

uint16_t mmu::read_par(const uint32_t a, const int run_mode)
{
	int      page = (a >> 1) & 7;
	bool     is_d = a & 16;
	uint16_t t    = pages[run_mode][is_d][page].par;

	return t;
}

void mmu::setMMR0_as_is(uint16_t value)
{
	MMR0 = value;
}

void mmu::setMMR0(uint16_t value)
{
	value &= ~(3 << 10);  // bit 10 & 11 always read as 0

	if (value & 1)
		value &= ~(7l << 13);  // reset error bits

	if (MMR0 & 0160000) {
		if ((value & 1) == 0)
			value &= 254;  // bits 7...1 are protected 
	}

	MMR0 = value;
}

void mmu::setMMR0Bit(const int bit)
{
	assert(bit != 10 && bit != 11);
	assert(bit < 16 && bit >= 0);

	MMR0 |= 1 << bit;
}

void mmu::clearMMR0Bit(const int bit)
{
	assert(bit != 10 && bit != 11);
	assert(bit < 16 && bit >= 0);

	MMR0 &= ~(1 << bit);
}

void mmu::setMMR2(const uint16_t value) 
{
	MMR2 = value;
}

void mmu::setMMR3(const uint16_t value) 
{
	MMR3 = value;
}

bool mmu::get_use_data_space(const int run_mode) const
{
	constexpr const int di_ena_mask[4] = { 4, 2, 0, 1 };

	return !!(MMR3 & di_ena_mask[run_mode]);
}

void mmu::clearMMR1()
{
	MMR1 = 0;
}

void mmu::addToMMR1(const int8_t delta, const uint8_t reg)
{
	assert(reg >= 0 && reg <= 7);
	assert(delta >= -2 && delta <= 2);

	assert((getMMR0() & 0160000) == 0);  // MMR1 should not be locked

	MMR1 <<= 8;

	MMR1 |= (delta & 31) << 3;
	MMR1 |= reg;
}

void mmu::write_pdr(const uint32_t a, const int run_mode, const uint16_t value, const word_mode_t word_mode)
{
	bool is_d = a & 16;
	int  page = (a >> 1) & 7;

	if (word_mode == wm_byte) {
		assert(a != 0 || value < 256);

		update_word(&pages[run_mode][is_d][page].pdr, a & 1, value);
	}
	else {
		pages[run_mode][is_d][page].pdr = value;
	}

	pages[run_mode][is_d][page].pdr &= ~(32768 + 128 /*A*/ + 64 /*W*/ + 32 + 16);  // set bit 4, 5 & 15 to 0 as they are unused and A/W are set to 0 by writes

	TRACE("mmu WRITE-I/O PDR run-mode %d: %c for %d: %o [%d]", run_mode, is_d ? 'D' : 'I', page, value, word_mode);
}

void mmu::write_par(const uint32_t a, const int run_mode, const uint16_t value, const word_mode_t word_mode)
{
	bool is_d = a & 16;
	int  page = (a >> 1) & 7;

	if (word_mode == wm_byte)
		update_word(&pages[run_mode][is_d][page].par, a & 1, value);
	else
		pages[run_mode][is_d][page].par = value;

	pages[run_mode][is_d][page].pdr &= ~(128 /*A*/ + 64 /*W*/);  // reset PDR A/W when PAR is written to

	TRACE("mmu WRITE-I/O PAR run-mode %d: %c for %d: %o (%07o)", run_mode, is_d ? 'D' : 'I', page, word_mode == wm_byte ? value & 0xff : value, pages[run_mode][is_d][page].par * 64);
}

uint16_t mmu::read_word(const uint16_t a)
{
	uint16_t v = 0;

	if (a >= ADDR_PDR_SV_START && a < ADDR_PDR_SV_END)
		v = read_pdr(a, 1);
	else if (a >= ADDR_PAR_SV_START && a < ADDR_PAR_SV_END)
		v = read_par(a, 1);
	else if (a >= ADDR_PDR_K_START && a < ADDR_PDR_K_END)
		v = read_pdr(a, 0);
	else if (a >= ADDR_PAR_K_START && a < ADDR_PAR_K_END)
		v = read_par(a, 0);
	else if (a >= ADDR_PDR_U_START && a < ADDR_PDR_U_END)
		v = read_pdr(a, 3);
	else if (a >= ADDR_PAR_U_START && a < ADDR_PAR_U_END)
		v = read_par(a, 3);

	return v;
}

uint8_t mmu::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr);

	if (addr & 1)
		return v >> 8;

	return v;
}

void mmu::write_word(const uint16_t a, const uint16_t value)
{
	// supervisor
	if (a >= ADDR_PDR_SV_START && a < ADDR_PDR_SV_END)
		write_pdr(a, 1, value, wm_word);
	else if (a >= ADDR_PAR_SV_START && a < ADDR_PAR_SV_END)
		write_par(a, 1, value, wm_word);
	// kernel
	else if (a >= ADDR_PDR_K_START && a < ADDR_PDR_K_END)
		write_pdr(a, 0, value, wm_word);
	else if (a >= ADDR_PAR_K_START && a < ADDR_PAR_K_END)
		write_par(a, 0, value, wm_word);
	// user
	else if (a >= ADDR_PDR_U_START && a < ADDR_PDR_U_END)
		write_pdr(a, 3, value, wm_word);
	else if (a >= ADDR_PAR_U_START && a < ADDR_PAR_U_END)
		write_par(a, 3, value, wm_word);
}

void mmu::write_byte(const uint16_t a, const uint8_t value)
{
	// supervisor
	if (a >= ADDR_PDR_SV_START && a < ADDR_PDR_SV_END)
		write_pdr(a, 1, value, wm_byte);
	else if (a >= ADDR_PAR_SV_START && a < ADDR_PAR_SV_END)
		write_par(a, 1, value, wm_byte);
	// kernel
	else if (a >= ADDR_PDR_K_START && a < ADDR_PDR_K_END)
		write_pdr(a, 0, value, wm_byte);
	else if (a >= ADDR_PAR_K_START && a < ADDR_PAR_K_END)
		write_par(a, 0, value, wm_byte);
	// user
	else if (a >= ADDR_PDR_U_START && a < ADDR_PDR_U_END)
		write_pdr(a, 3, value, wm_byte);
	else if (a >= ADDR_PAR_U_START && a < ADDR_PAR_U_END)
		write_par(a, 3, value, wm_byte);
}

void mmu::trap_if_odd(const uint16_t a, const int run_mode, const d_i_space_t space, const bool is_write)
{
	int page = a >> 13;

	if (is_write)
		set_page_trapped(run_mode, space == d_space, page);

	MMR0 &= ~(7 << 1);
	MMR0 |= page << 1;
}

memory_addresses_t mmu::calculate_physical_address(const int run_mode, const uint16_t a) const
{
	const uint8_t apf = a >> 13; // active page field

	if (is_enabled() == false) {
		bool is_psw = a == ADDR_PSW;
		return { a, apf, a, is_psw, a, is_psw };
	}

	uint32_t physical_instruction = get_physical_memory_offset(run_mode, 0, apf);
	uint32_t physical_data        = get_physical_memory_offset(run_mode, 1, apf);

	uint16_t p_offset = a & 8191;  // page offset

	physical_instruction += p_offset;
	physical_data        += p_offset;

	if ((getMMR3() & 16) == 0) {  // offset is 18bit
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

std::pair<trap_action_t, int> mmu::get_trap_action(const int run_mode, const bool d, const int apf, const bool is_write)
{
	const int     access_control = get_access_control(run_mode, d, apf);

	trap_action_t trap_action    = T_PROCEED;

	switch(access_control) {
		case 0:
			trap_action = T_ABORT_4;
			break;
		case 1:
			trap_action = is_write ? T_ABORT_4 : T_TRAP_250;
			break;

		case 2:
			if (is_write)
				trap_action = T_ABORT_4;
			break;
		case 3:
			trap_action = T_ABORT_4;
			break;
		case 4:
			trap_action = T_TRAP_250;
			break;
		case 5:
			if (is_write)
				trap_action = T_TRAP_250;
			break;
		case 6:
			// proceed
			break;

		case 7:
			trap_action = T_ABORT_4;
			break;
	}

	return { trap_action, access_control };
}

void mmu::mmudebug(const uint16_t a)
{
	for(int rm=0; rm<4; rm++) {
		auto ma = calculate_physical_address(rm, a);

		TRACE("RM %d, a: %06o, apf: %d, PI: %08o (PSW: %d), PD: %08o (PSW: %d)", rm, ma.virtual_address, ma.apf, ma.physical_instruction, ma.physical_instruction_is_psw, ma.physical_data, ma.physical_data_is_psw);
	}
}

void mmu::verify_page_access(cpu *const c, const uint16_t virt_addr, const int run_mode, const bool d, const int apf, const bool is_write)
{
	const auto [ trap_action, access_control ] = get_trap_action(run_mode, d, apf, is_write);

	if (trap_action == T_PROCEED)
		return;

	if (is_write)
		set_page_trapped(run_mode, d, apf);

	if (is_locked() == false) {
		uint16_t temp = getMMR0();

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

		setMMR0_as_is(temp);

		TRACE("MMR0: %06o", temp);
	}

	if (trap_action == T_TRAP_250) {
		TRACE("Page access %d (for virtual address %06o): trap 0250", access_control, virt_addr);

		c->trap(0250);  // trap

		throw 5;
	}
	else {  // T_ABORT_4
		TRACE("Page access %d (for virtual address %06o): trap 004", access_control, virt_addr);

		c->trap(004);  // abort

		throw 5;
	}
}

void mmu::verify_access_valid(cpu *const c, const uint32_t m_offset, const int run_mode, const bool d, const int apf, const bool is_io, const bool is_write)
{
	if (m_offset >= m->get_memory_size() && !is_io) [[unlikely]] {
		TRACE("TRAP(04) (throw 6) on address %08o", m_offset);

		if (is_locked() == false) {
			uint16_t temp = getMMR0();

			temp &= 017777;
			temp |= 1l << 15;  // non-resident

			temp &= ~14;  // add current page
			temp |= apf << 1;

			temp &= ~(3 << 5);
			temp |= run_mode << 5;

			setMMR0_as_is(temp);
		}

		if (is_write)
			set_page_trapped(run_mode, d, apf);

		c->trap(04);

		throw 6;
	}
}

void mmu::verify_page_length(cpu *const c, const uint16_t virt_addr, const int run_mode, const bool d, const int apf, const bool is_write)
{
	uint16_t pdr_len = get_pdr_len(run_mode, d, apf);
	uint16_t pdr_cmp = (virt_addr >> 6) & 127;

	bool direction   = get_pdr_direction(run_mode, d, apf);

	if ((pdr_cmp > pdr_len && direction == false) || (pdr_cmp < pdr_len && direction == true)) [[unlikely]] {
		TRACE("mmu::calculate_physical_address::p_offset %o versus %o direction %d", pdr_cmp, pdr_len, direction);
		TRACE("TRAP(0250) (throw 7) on address %06o", virt_addr);

		c->trap(0250);  // invalid access

		if (is_locked() == false) {
			uint16_t temp = getMMR0();

			temp &= 017777;
			temp |= 1 << 14;  // length

			temp &= ~14;  // add current page
			temp |= apf << 1;

			temp &= ~(3 << 5);
			temp |= run_mode << 5;

			temp &= ~(1 << 4);
			temp |= d << 4;

			setMMR0_as_is(temp);
		}

		if (is_write)
			set_page_trapped(run_mode, d, apf);

		throw 7;
	}
}

uint32_t mmu::calculate_physical_address(cpu *const c, const int run_mode, const uint16_t a, const bool trap_on_failure, const bool is_write, const d_i_space_t space)
{
	uint32_t m_offset = a;

	if (is_enabled() || (is_write && (getMMR0() & (1 << 8 /* maintenance check */)))) {
		uint8_t  apf      = a >> 13; // active page field

		bool     d        = space == d_space && get_use_data_space(run_mode);

		uint16_t p_offset = a & 8191;  // page offset

		m_offset  = get_physical_memory_offset(run_mode, d, apf);

		m_offset += p_offset;

		if ((getMMR3() & 16) == 0)  // off is 18bit
			m_offset &= 0x3ffff;

		if (trap_on_failure) {
			verify_page_access(c, a, run_mode, d, apf, is_write);

			// e.g. ram or i/o, not unmapped
			uint32_t io_base  = get_io_base();
			bool     is_io    = m_offset >= io_base;

			verify_access_valid(c, m_offset, run_mode, d, apf, is_io, is_write);

			verify_page_length(c, a, run_mode, d, apf, is_write);
		}
	}

	return m_offset;
}

JsonVariant mmu::add_par_pdr(const int run_mode, const bool is_d) const
{
	JsonVariant j;

	JsonArray ja_par;
	for(int i=0; i<8; i++)
		ja_par.add(pages[run_mode][is_d][i].par);
	j["par"] = ja_par;

	JsonArray ja_pdr;
	for(int i=0; i<8; i++)
		ja_pdr.add(pages[run_mode][is_d][i].pdr);
	j["pdr"] = ja_pdr;

	return j;
}

JsonVariant mmu::serialize() const
{
	JsonVariant j;

	for(int run_mode=0; run_mode<4; run_mode++) {
		if (run_mode == 2)
			continue;

		for(int is_d=0; is_d<2; is_d++)
			j[format("runmode_%d_d_%d", run_mode, is_d)] = add_par_pdr(run_mode, is_d);
	}

        j["MMR0"]   = MMR0;
        j["MMR1"]   = MMR1;
        j["MMR2"]   = MMR2;
        j["MMR3"]   = MMR3;
        j["CPUERR"] = CPUERR;
        j["PIR"]    = PIR;
        j["CSR"]    = CSR;

	return j;
}

void mmu::set_par_pdr(const JsonVariant j_in, const int run_mode, const bool is_d)
{
	JsonArray j_par = j_in["par"];
	int       i_par = 0;
	for(auto v: j_par)
		pages[run_mode][is_d][i_par++].par = v;

	JsonArray j_pdr = j_in["pdr"];
	int       i_pdr = 0;
	for(auto v: j_pdr)
		pages[run_mode][is_d][i_pdr++].pdr = v;
}

mmu *mmu::deserialize(const JsonVariant j, memory *const mem)
{
	mmu *m = new mmu();
	m->begin(mem);

	for(int run_mode=0; run_mode<4; run_mode++) {
		if (run_mode == 2)
			continue;

		for(int is_d=0; is_d<2; is_d++)
			m->set_par_pdr(j[format("runmode_%d_d_%d", run_mode, is_d)].as<JsonVariant>(), run_mode, is_d);
	}

        m->MMR0   = j["MMR0"];
        m->MMR1   = j["MMR1"];
        m->MMR2   = j["MMR2"];
        m->MMR3   = j["MMR3"];
        m->CPUERR = j["CPUERR"];
        m->PIR    = j["PIR"];
        m->CSR    = j["CSR"];

	return m;
}
