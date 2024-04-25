#pragma once

#include <cstdint>


typedef struct {
	uint16_t par;
	uint16_t pdr;
} page_t;

class mmu
{
private:
	// 8 pages, D/I, 3 modes and 1 invalid mode
	page_t   pages[4][2][8];

	uint16_t MMR0 { 0 };
	uint16_t MMR1 { 0 };
	uint16_t MMR2 { 0 };
	uint16_t MMR3 { 0 };
	uint16_t CPUERR { 0 };
	uint16_t PIR { 0 };
	uint16_t CSR { 0 };

public:
	mmu();
	virtual ~mmu();

	void     reset();

	bool     is_enabled() const { return MMR0 & 1; }
	bool     is_locked()  const { return !!(MMR0 & 0160000); }

	void     set_page_trapped   (const int run_mode, const bool d, const int apf) { pages[run_mode][d][apf].pdr |= 1 << 7; }
	void     set_page_written_to(const int run_mode, const bool d, const int apf) { pages[run_mode][d][apf].pdr |= 1 << 6; }
	int      get_access_control (const int run_mode, const bool d, const int apf) { return pages[run_mode][d][apf].pdr & 7; }
	int      get_pdr_len        (const int run_mode, const bool d, const int apf) { return (pages[run_mode][d][apf].pdr >> 8) & 127; }
	int      get_pdr_direction  (const int run_mode, const bool d, const int apf) { return pages[run_mode][d][apf].pdr & 8; }
	uint32_t get_physical_memory_offset(const int run_mode, const bool d, const int apf) const { return pages[run_mode][d][apf].par * 64; }
	bool     get_use_data_space(const int run_mode) const;

	uint16_t getMMR0() const { return MMR0; }
	uint16_t getMMR1() const { return MMR1; }
	uint16_t getMMR2() const { return MMR2; }
	uint16_t getMMR3() const { return MMR3; }
	uint16_t getMMR(int nr) const { const uint16_t *const mmrs[] { &MMR0, &MMR1, &MMR2, &MMR3 }; return *mmrs[nr]; }

	void     setMMR0(const uint16_t value);
	void     setMMR1(const uint16_t value);
	void     setMMR2(const uint16_t value);
	void     setMMR3(const uint16_t value);

	bool     isMMR1Locked() const { return !!(MMR0 & 0160000); }
	void     clearMMR1();
	void     addToMMR1(const int8_t delta, const uint8_t reg);

	void     setMMR0Bit(const int bit);
	void     clearMMR0Bit(const int bit);

	uint16_t getCPUERR() const { return CPUERR; }
	void     setCPUERR(const uint16_t v) { CPUERR = v; }

	uint16_t getPIR() const { return PIR; };
	void     setPIR(const uint16_t v) { PIR = v; }

	uint16_t read_par(const uint32_t a, const int run_mode, const word_mode_t word_mode, const bool peek_only);
	uint16_t read_pdr(const uint32_t a, const int run_mode, const word_mode_t word_mode, const bool peek_only);

	void     write_pdr(const uint32_t a, const int run_mode, const uint16_t value, const word_mode_t word_mode);
	void     write_par(const uint32_t a, const int run_mode, const uint16_t value, const word_mode_t word_mode);
};
