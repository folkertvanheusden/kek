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

void mmu::begin()
{
	reset();
}

void mmu::reset()
{
	memset(pages, 0x00, sizeof pages);

	CPUERR = MMR0 = MMR1 = MMR2 = MMR3 = PIR = CSR = 0;
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

	DOLOG(debug, false, "mmu WRITE-I/O PDR run-mode %d: %c for %d: %o [%d]", run_mode, is_d ? 'D' : 'I', page, value, word_mode);
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

	DOLOG(debug, false, "mmu WRITE-I/O PAR run-mode %d: %c for %d: %o (%07o)", run_mode, is_d ? 'D' : 'I', page, word_mode == wm_byte ? value & 0xff : value, pages[run_mode][is_d][page].par * 64);
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

#if IS_POSIX
void mmu::add_par_pdr(json_t *const target, const int run_mode, const bool is_d, const std::string & name) const
{
	json_t *j = json_object();

	json_t *ja_par = json_array();
	for(int i=0; i<8; i++)
		json_array_append(ja_par, json_integer(pages[run_mode][is_d][i].par));
	json_object_set(j, "par", ja_par);

	json_t *ja_pdr = json_array();
	for(int i=0; i<8; i++)
		json_array_append(ja_pdr, json_integer(pages[run_mode][is_d][i].pdr));
	json_object_set(j, "pdr", ja_pdr);

	json_object_set(target, name.c_str(), j);
}

json_t *mmu::serialize() const
{
	json_t *j = json_object();

	for(int run_mode=0; run_mode<4; run_mode++) {
		if (run_mode == 2)
			continue;

		for(int is_d=0; is_d<2; is_d++)
			add_par_pdr(j, run_mode, is_d, format("runmode_%d_d_%d", run_mode, is_d));
	}

        json_object_set(j, "MMR0", json_integer(MMR0));
        json_object_set(j, "MMR1", json_integer(MMR1));
        json_object_set(j, "MMR2", json_integer(MMR2));
        json_object_set(j, "MMR3", json_integer(MMR3));
        json_object_set(j, "CPUERR", json_integer(CPUERR));
        json_object_set(j, "PIR", json_integer(PIR));
        json_object_set(j, "CSR", json_integer(CSR));

	return j;
}

void mmu::set_par_pdr(const json_t *const j_in, const int run_mode, const bool is_d, const std::string & name)
{
	json_t *j = json_object_get(j_in, name.c_str());

	json_t *j_par = json_object_get(j, "par");
	for(int i=0; i<8; i++)
		pages[run_mode][is_d][i].par = json_integer_value(json_array_get(j_par, i));
	json_t *j_pdr = json_object_get(j, "pdr");
	for(int i=0; i<8; i++)
		pages[run_mode][is_d][i].pdr = json_integer_value(json_array_get(j_pdr, i));
}

mmu *mmu::deserialize(const json_t *const j)
{
	mmu *m = new mmu();
	m->begin();

	for(int run_mode=0; run_mode<4; run_mode++) {
		if (run_mode == 2)
			continue;

		for(int is_d=0; is_d<2; is_d++)
			m->set_par_pdr(j, run_mode, is_d, format("runmode_%d_d_%d", run_mode, is_d));
	}

        m->MMR0   = json_integer_value(json_object_get(j, "MMR0"));
        m->MMR1   = json_integer_value(json_object_get(j, "MMR1"));
        m->MMR2   = json_integer_value(json_object_get(j, "MMR2"));
        m->MMR3   = json_integer_value(json_object_get(j, "MMR3"));
        m->CPUERR = json_integer_value(json_object_get(j, "CPUERR"));
        m->PIR    = json_integer_value(json_object_get(j, "PIR"));
        m->CSR    = json_integer_value(json_object_get(j, "CSR"));

	return m;
}
#endif
