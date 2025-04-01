#pragma once

#include "gen.h"
#include <ArduinoJson.h>
#include <cstdint>
#include <string>
#include "cpu.h"
#include "device.h"
#include "memory.h"


#define ADDR_PDR_SV_START 0172200
#define ADDR_PDR_SV_END   0172240
#define ADDR_PAR_SV_START 0172240
#define ADDR_PAR_SV_END   0172300

#define ADDR_PDR_K_START 0172300
#define ADDR_PDR_K_END   0172340
#define ADDR_PAR_K_START 0172340
#define ADDR_PAR_K_END   0172400

#define ADDR_PDR_U_START 0177600
#define ADDR_PDR_U_END   0177640
#define ADDR_PAR_U_START 0177640
#define ADDR_PAR_U_END   0177700


typedef enum { T_PROCEED, T_ABORT_4, T_TRAP_250 } trap_action_t;

typedef struct {
	uint16_t virtual_address;
	uint8_t  apf;  // active page field
	uint32_t physical_instruction;
	bool     physical_instruction_is_psw;
	uint32_t physical_data;
	bool     physical_data_is_psw;
} memory_addresses_t;

typedef struct {
	uint16_t par;
	uint16_t pdr;
} page_t;

class mmu : public device
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

	memory  *m { nullptr };
	cpu     *c { nullptr };

	JsonDocument add_par_pdr(const int run_mode, const bool is_d) const;
	void set_par_pdr(const JsonVariantConst j_in, const int run_mode, const bool is_d);

	void verify_page_access (const uint16_t virt_addr, const int run_mode, const bool d, const int apf, const bool is_write);
	void verify_access_valid(const uint32_t m_offset,  const int run_mode, const bool d, const int apf, const bool is_io, const bool is_write);
	void verify_page_length (const uint16_t virt_addr, const int run_mode, const bool d, const int apf, const bool is_write);

public:
	mmu();
	virtual ~mmu();

	void     begin(memory *const m, cpu *const c);

	JsonDocument serialize() const;
	static mmu *deserialize(const JsonVariantConst j, memory *const m, cpu *const c);

	void     mmudebug(const uint16_t a);

	void     reset() override;

	void     dump_par_pdr(console *const cnsl, const int run_mode, const bool d, const std::string & name, const int state, const std::optional<int> & selection) const;
	void     show_state(console *const cnsl) const override;

	bool     is_enabled() const { return MMR0 & 1; }
	bool     is_locked()  const { return MMR0 & 0160000; }

	void     set_page_trapped   (const int run_mode, const bool d, const int apf) { pages[run_mode][d][apf].pdr |= 1 << 7; }
	void     set_page_written_to(const int run_mode, const bool d, const int apf) { pages[run_mode][d][apf].pdr |= 1 << 6; }
	int      get_access_control (const int run_mode, const bool d, const int apf) { return pages[run_mode][d][apf].pdr & 7; }
	int      get_pdr_len        (const int run_mode, const bool d, const int apf) { return (pages[run_mode][d][apf].pdr >> 8) & 127; }
	int      get_pdr_direction  (const int run_mode, const bool d, const int apf) { return pages[run_mode][d][apf].pdr & 8; }
	uint32_t get_physical_memory_offset(const int run_mode, const bool d, const int apf) const { return pages[run_mode][d][apf].par * 64; }
	bool     get_use_data_space(const int run_mode) const;
	uint32_t get_io_base() const { return is_enabled() ? (getMMR3() & 16 ? 017760000 : 0760000) : 0160000; }

	memory_addresses_t            calculate_physical_address(const int run_mode, const uint16_t a) const;
	std::pair<trap_action_t, int> get_trap_action(const int run_mode, const bool d, const int apf, const bool is_write);
	uint32_t                      calculate_physical_address(const int run_mode, const uint16_t a, const bool is_write, const d_i_space_t space);

	uint16_t getMMR0() const { return MMR0; }
	uint16_t getMMR1() const { return MMR1; }
	uint16_t getMMR2() const { return MMR2; }
	uint16_t getMMR3() const { return MMR3; }
	uint16_t getMMR(int nr) const { const uint16_t *const mmrs[] { &MMR0, &MMR1, &MMR2, &MMR3 }; return *mmrs[nr]; }

	void     setMMR0_as_is(uint16_t value);
	void     setMMR0(const uint16_t value);
	void     setMMR1(const uint16_t value);
	void     setMMR2(const uint16_t value);
	void     setMMR3(const uint16_t value);

	bool     isMMR1Locked() const { return !!(MMR0 & 0160000); }
	void     clearMMR1();
	void     addToMMR1(const int8_t delta, const uint8_t reg);

	void     setMMR0Bit(const int bit);
	void     clearMMR0Bit(const int bit);

	void     trap_if_odd(const uint16_t a, const int run_mode, const d_i_space_t space, const bool is_write);

	uint16_t getCPUERR() const { return CPUERR; }
	void     setCPUERR(const uint16_t v) { CPUERR = v; }

	uint16_t getPIR() const { return PIR; };
	void     setPIR(const uint16_t v) { PIR = v; }

	uint16_t read_par(const uint32_t a, const int run_mode);
	uint16_t read_pdr(const uint32_t a, const int run_mode);

	void     write_pdr(const uint32_t a, const int run_mode, const uint16_t value, const word_mode_t word_mode);
	void     write_par(const uint32_t a, const int run_mode, const uint16_t value, const word_mode_t word_mode);

	uint8_t  read_byte(const uint16_t addr) override;
	uint16_t read_word(const uint16_t addr) override;

	void     write_byte(const uint16_t addr, const uint8_t v) override;
	void     write_word(const uint16_t addr, uint16_t v)      override;
};
