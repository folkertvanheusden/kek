#include "deqna.h"


deqna::deqna(bus *const b, const uint8_t mac_address[6])
{
	memcpy(this->mac_address, mac_address, sizeof this->mac_address);
	reset();
}

deqna::~deqna()
{
}

void deqna::reset()
{
	DOLOG(info, false, "deqna reset");

	memset(registers, 0x00, sizeof registers);
	registers[6] = 0774;
	registers[7] = 0x100 |  // IL is on initially
		32 |  // receive list invalid
		16;  // transmit list invalid
}

void deqna::show_state(console *const cnsl) const
{
}

uint16_t deqna::read_word(const uint16_t addr)
{
	int      reg_nr = (addr - DEQNA_BASE) / 2;
	uint16_t rc     = registers[reg_nr];

	if (reg_nr < 6)  // MAC address in low byte from first 6 words
		rc = mac_address[reg_nr];

	if (reg_nr == 7)  // CSR
		rc |= 0x2000;  // carrier detected

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
		registers[7] &= ~0x8000;  // clear RI (receive interrupt request)
	}
	else if (addr == DEQNA_RX_BDLH) {
		registers[7] &= ~32;  // RX buffers set, no more invalid
	}
	else if (addr == DEQNA_TX_BDLH) {
		registers[7] &= ~16;  // TX buffers set, no more invalid
	}
	else if (addr == DEQNA_VECTOR) {
		registers[reg_nr] &= 0x7fd;  // mask off unused bits but keep QE_VEC_ID enabled
	}
}
