#include <cassert>
#include <cstring>

#include "gen.h"
#include "log.h"
#include "mmu.h"
#include "utils.h"


mmu::mmu()
{
	reset();
}

mmu::~mmu()
{
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

uint16_t mmu::readWord(const uint16_t a)
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

uint8_t mmu::readByte(const uint16_t addr)
{
	uint16_t v = readWord(addr);

	if (addr & 1)
		return v >> 8;

	return v;
}

void mmu::writeWord(const uint16_t a, const uint16_t value)
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

void mmu::writeByte(const uint16_t a, const uint8_t value)
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
