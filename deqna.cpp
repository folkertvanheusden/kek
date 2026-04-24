#include "deqna.h"


deqna::deqna(bus *const b, const uint8_t mac_address[6])
{
	memcpy(this->mac_address, mac_address, sizeof mac_address);
}

deqna::~deqna()
{
}

void deqna::reset()
{
	memset(registers, 0x00, sizeof registers);
	// TODO set mac address
}

void deqna::show_state(console *const cnsl) const
{
}

uint16_t deqna::read_word(const uint16_t addr)
{
	int      reg_nr = (addr - DEQNA_BASE) / 2;
	uint16_t rc     = registers[reg_nr];

	if (reg_nr < 6)
		rc = mac_address[reg_nr];

	DOLOG(info, false, "deqna read from %06o (%d): %06o", addr, reg_nr, rc);

	return rc;
}

void deqna::write_byte(const uint16_t addr, const uint8_t v)
{
	int reg_nr = (addr - DEQNA_BASE) / 2;
	DOLOG(info, false, "deqna write %03o to %06o (%d)", v, addr, reg_nr);
}

void deqna::write_word(const uint16_t addr, uint16_t v)
{
	int reg_nr = (addr - DEQNA_BASE) / 2;
	DOLOG(info, false, "deqna write %06o to %06o (%d)", v, addr, reg_nr);

	registers[reg_nr] = v;

	if (addr == DEQNA_CSR) {
		registers[reg_nr] |= 0x2000;  // carrier detected
	}
	else if (addr == DEQNA_VECTOR) {
		registers[reg_nr] &= 0x7fd;  // mask off unused bits but keep QE_VEC_ID enabled
	}
}
