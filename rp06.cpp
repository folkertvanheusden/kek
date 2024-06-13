// (C) 2024 by Folkert van Heusden
// Released under MIT license
// Some of the code is translated from Neil Webber's PDP11/70 emulator

#include <errno.h>
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "error.h"
#include "gen.h"
#include "log.h"
#include "rp06.h"
#include "utils.h"


constexpr const unsigned NSECT       = 22;               // sectors per track
constexpr const unsigned NTRAC       = 19;               // tracks per cylinder
constexpr const unsigned SECTOR_SIZE = 512;
constexpr const uint16_t default_DS  = uint16_t(rp06::ds_bits::DPR) /* drive present */ | uint16_t(rp06::ds_bits::MOL) /* medium on-line */ | uint16_t(rp06::ds_bits::VV) /* volume valid */ | uint16_t(rp06::ds_bits::DRY) /* drive ready */;

constexpr const char *regnames[] { "Control", "Status", "Error register 1", "Maintenance", "Attention summary", "Desired sector/track address", "Error register 1", "Look ahead", "Drive type", "Serial no", "Offset", "Desired cylinder address", "Current cylinder address", "Error register 2", "Error register 3", "ECC position", "ECC pattern" };

rp06::rp06(bus *const b, std::atomic_bool *const disk_read_activity, std::atomic_bool *const disk_write_activity) :
	b(b),
	disk_read_activity (disk_read_activity ),
	disk_write_activity(disk_write_activity)
{
}

rp06::~rp06()
{
}

void rp06::begin()
{
	reset();
}

void rp06::reset()
{
	memset(registers, 0x00, sizeof registers);

	registers[reg_num(RP06_DS)] = default_DS;
}

void rp06::show_state(console *const cnsl) const
{
}

JsonDocument rp06::serialize() const
{
	JsonDocument j;

	return j;
}

rp06 *rp06::deserialize(const JsonVariantConst j, bus *const b)
{
	rp06 *r = new rp06(b, nullptr, nullptr);
	r->begin();

	return r;
}

uint8_t rp06::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t rp06::read_word(const uint16_t addr)
{
	const int reg   = reg_num(addr);
	uint16_t  value = registers[reg];

	if (addr == RP06_CS1)
		value |= uint16_t(rp06::cs1_bits::RDY /* ready */);
	else if (addr == RP06_DS)
		value |= default_DS;

	TRACE("RP06: read \"%s\"/%o: %06o", regnames[reg], addr, value);

	return value;
}

int rp06::reg_num(uint16_t addr) const
{
	return (addr - RP06_BASE) / 2;
}

void rp06::write_byte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[reg_num(addr)];

	if (addr & 1) {
		vtemp &= 0x00ff;
		vtemp |= v << 8;
	}
	else {
		vtemp &= 0xff00;
		vtemp |= v;
	}

	write_word(addr & ~1, vtemp);
}

uint32_t rp06::compute_offset() const
{
        // cyl num, track num, sector num, which were written like this:
        uint16_t cn = registers[reg_num(RP06_DC)];
        uint16_t tn = (registers[reg_num(RP06_DA)] >> 8) & 0377;
        uint16_t sn = registers[reg_num(RP06_DA)] & 0377;

        // each cylinder is NSECT*NTRAC sectors
        // each track is NSECT sectors
        uint32_t offs = cn * NSECT * NTRAC;
        offs += tn * NSECT;
        offs += sn;
        offs *= SECTOR_SIZE;

        return offs;
}

uint32_t rp06::getphysaddr() const
{
	// low 16 bits in UBA, and tack on A16/A17
	bool cur_A16 = registers[reg_num(RP06_CS1)] & uint16_t(rp06::cs1_bits::A16);
	bool cur_A17 = registers[reg_num(RP06_CS1)] & uint16_t(rp06::cs1_bits::A17);

	uint16_t cur_A1621 = 0;

	// but also bits may be found in bae... the assumption here is
	// if these bits are non-zero they override A16/A17 but they
	// really need to be consistent...
	if (registers[reg_num(RP06_BAE)]) {
		cur_A16 = false;  // subsumed in A1621
		cur_A17 = false;  // subsumed
		cur_A1621 = registers[reg_num(RP06_BAE)] & 077;
	}

	return registers[reg_num(RP06_UBA)] | (cur_A16 << 16) | (cur_A17 << 17) | (cur_A1621 << 16);
}

void rp06::write_word(const uint16_t addr, uint16_t v)
{
	const int reg = reg_num(addr);

	TRACE("RP06: write \"%s\"/%06o: %06o", regnames[reg], addr, v);

        registers[reg] = v;

	if (addr == RP06_CS1) {
	        if (registers[reg_num(RP06_CS1)] & uint16_t(rp06::cs1_bits::RDY))  // ready
			registers[reg_num(RP06_AS)] = 1;  // this is very bogus but maybe works for now

		if (v & 1) {
			bool     generate_interrupt = false;
			uint16_t function_code      = v & 62;

			registers[reg_num(RP06_CS1)] &= ~(function_code | uint16_t(rp06::cs1_bits::GO) | uint16_t(rp06::cs1_bits::TRE));

			if (function_code == 006 || function_code == 012 || function_code == 016 ||
					function_code == 020 || function_code == 022) {
				DOLOG(debug, false, "RP06: ignoring command %03o", function_code);

				registers[reg_num(RP06_CS1)] |= uint16_t(rp06::cs1_bits::RDY);  // drive ready

				generate_interrupt = true;
			}
			else if (function_code == 030) {  // SEARCH
				registers[reg_num(RP06_CS1)] |= uint16_t(rp06::cs1_bits::RDY);  // drive ready
				registers[reg_num(RP06_CC)]   = registers[reg_num(RP06_DC)];

				generate_interrupt = true;
			}
			else if (function_code == 060 || function_code == 070) {  // WRITE (060), READ (070)
				uint32_t offs = compute_offset();
				uint32_t addr = getphysaddr();

				uint32_t nw   = 65536 - registers[reg_num(RP06_WC)];
				uint32_t nb   = nw * 2;

				uint8_t  xfer_buffer[SECTOR_SIZE] { };
				uint32_t end_offset = offs + nb;
				for(uint32_t cur_offset = offs; cur_offset<end_offset; cur_offset += SECTOR_SIZE) {
					uint32_t cur_n = std::min(end_offset - cur_offset, SECTOR_SIZE);

					if (function_code == 070) {
						DOLOG(debug, false, "RP06: reading %u bytes from %u (dec) to %06o (oct)", cur_n, offs, addr);

						if (!fhs.at(0)->read(cur_offset, cur_n, xfer_buffer, SECTOR_SIZE)) {
							DOLOG(ll_error, true, "RP06 read error %s from %u", strerror(errno), cur_offset);
							//registers[(RK05_ERROR - RK05_BASE) / 2] |= 32;  // non existing sector
							//registers[(RK05_CS - RK05_BASE) / 2] |= 3 << 14;  // an error occured
							break;
						}

						for(uint32_t i=0; i<cur_n; i++)
							b->writeUnibusByte(addr++, xfer_buffer[i]);
					}
					else {
						DOLOG(debug, false, "RP06: writing %u bytes to %u (dec) from %06o (oct)", cur_n, offs, addr);

						for(uint32_t i=0; i<cur_n; i++)
							xfer_buffer[i] = b->readUnibusByte(addr++);

						if (!fhs.at(0)->write(cur_offset, cur_n, xfer_buffer, SECTOR_SIZE)) {
							DOLOG(ll_error, true, "RP06 write error %s from %u", strerror(errno), cur_offset);
							//registers[(RK05_ERROR - RK05_BASE) / 2] |= 32;  // non existing sector
							//registers[(RK05_CS - RK05_BASE) / 2] |= 3 << 14;  // an error occured
							break;
						}
					}
				}

				registers[reg_num(RP06_WC)]   = 0;
				registers[reg_num(RP06_CS1)] |= uint16_t(rp06::cs1_bits::RDY);  // drive ready

				generate_interrupt = true;
			}
			else {
				DOLOG(warning, true, "RP06: command %03o not implemented", function_code);
			}

			if (generate_interrupt) {
				if (registers[reg_num(RP06_CS1)] & uint16_t(rp06::cs1_bits::IE))  // IE? (interrupt enable)
					b->getCpu()->queue_interrupt(5, 0254);
			}
		}
	}
	else {
		DOLOG(debug, false, "RP06: write ignored to %06o", addr);
	}
}
