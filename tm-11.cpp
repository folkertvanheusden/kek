// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <errno.h>
#include <string.h>

#include "tm-11.h"
#include "gen.h"
#include "log.h"
#include "memory.h"
#include "utils.h"

tm_11::tm_11(bus *const b): m(b->getRAM()), b(b)
{
}

tm_11::~tm_11()
{
	if (fh)
		fclose(fh);
}

void tm_11::unload()
{
	if (fh) {
		fclose(fh);
		fh = nullptr;
	}

	tape_file.clear();

	reset(true);
}

void tm_11::load(const std::string & file)
{
	if (fh)
		fclose(fh);

	fh = fopen(file.c_str(), "rb");
	tape_file = file;

	reset(true);
}

void tm_11::show_state(console *const cnsl) const
{
	cnsl->put_string_lf(format("MTS   : %06o", registers[0]));
	cnsl->put_string_lf(format("MTC   : %06o", registers[1]));
	cnsl->put_string_lf(format("MTBRC : %06o", registers[2]));
	cnsl->put_string_lf(format("MTCMA : %06o", registers[3]));
	cnsl->put_string_lf(format("MTD   : %06o", registers[4]));
	cnsl->put_string_lf(format("MTRD  : %06o", registers[5]));
	cnsl->put_string_lf(format("offset: %ld" , ftell(fh)));
	cnsl->put_string_lf(format("file  : %s"  , tape_file.c_str()));
}

void tm_11::reset(const bool hard)
{
	if (hard) {
		memset(registers,   0x00, sizeof registers  );
		memset(xfer_buffer, 0x00, sizeof xfer_buffer);
	}

	if (fh && fseek(fh, 0, SEEK_SET) != 0)
		DOLOG(warning, false, "TM-11 rewind error");
}

uint8_t tm_11::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr & ~1);
	if (addr & 1)
		return v >> 8;
	return v;
}

uint16_t tm_11::read_word(const uint16_t addr)
{
	const int reg = (addr - TM_11_BASE) / 2;
	uint16_t vtemp = registers[reg];

	if (addr == TM_11_MTS) {
		setBit(vtemp, 15, false); // ILC
		setBit(vtemp, 14, false); // EOC
		setBit(vtemp, 13, false); // CRE
		setBit(vtemp, 12, false); // PAE
		setBit(vtemp, 11, false); // BGL
		setBit(vtemp, 10, false); // EOT
		setBit(vtemp,  9, false); // RLE
		setBit(vtemp,  8, false); // BTE
		setBit(vtemp,  7, false); // NXM
		setBit(vtemp,  6, true);  // SELR
		setBit(vtemp,  5, false); // BOT - beginning of tape
		setBit(vtemp,  4, false); // 7CH
		setBit(vtemp,  3, false); // SDWN
		setBit(vtemp,  2, false); // WRL - write lock
		setBit(vtemp,  1, false); // RWS
		setBit(vtemp,  0, true);  // TUR - tape unit ready
	}
	else if (addr == TM_11_MTC) {
		registers[reg] ^= 1 << 7; // CU RDY
	}
	else if (addr == TM_11_MTBRC) { // record length
	}

	DOLOG(debug, false, "TM-11 read addr %o: %o", addr, vtemp);

	return vtemp;
}

void tm_11::write_byte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - TM_11_BASE) / 2];

	if (addr & 1) {
		vtemp &= ~0xff00;
		vtemp |= v << 8;
	}
	else {
		vtemp &= ~0x00ff;
		vtemp |= v;
	}

	write_word(addr, vtemp);
}

std::optional<unsigned> tm_11::find_data_record_forward()
{
	for(;;) {
		uint8_t header_buffer[4] { };
		if (fread(header_buffer, 1, 4, fh) != 4)
			break;

		uint32_t header = header_buffer[0] + (header_buffer[1] << 8) + (header_buffer[2] << 16) + (header_buffer[3] << 24);
		uint32_t length = header & 0x0fff;
		uint8_t  meta   = header >> 24;

		if (length > 0) {
			if (meta == 0) {
				DOLOG(debug, false, "TM-11 found record of size %u at offset %ld", length, ftell(fh));
				return { length };
			}
			if (fseek(fh, length + 4, SEEK_CUR) != 0)  // including trailer
				break;
		}
	}

	DOLOG(warning, false, "TM-11 seek error");

	return { };
}

std::optional<unsigned> tm_11::find_data_record_backward()
{
	for(;;) {
		if (fseek(fh, -4, SEEK_CUR) != 0)
			break;

		uint8_t header_buffer[4] { };
		if (fread(header_buffer, 1, 4, fh) != 4)
			break;

		uint32_t header = header_buffer[0] + (header_buffer[1] << 8) + (header_buffer[2] << 16) + (header_buffer[3] << 24);
		uint32_t length = header & 0x0fff;
		uint8_t  meta   = header >> 24;

		if (length > 0) {
			if (fseek(fh, -(length + 4), SEEK_CUR) != 0)
				break;

			if (meta == 0) {
				DOLOG(debug, false, "TM-11 found record of size %u at offset %ld", length, ftell(fh));
				return { length };
			}

			if (fseek(fh, -4, SEEK_CUR) != 0)
				break;
		}
	}

	DOLOG(warning, false, "TM-11 seek error");

	return { };
}

bool tm_11::skip_trailer_forward()
{
	return fseek(fh, 4, SEEK_CUR) == 0;
}

bool tm_11::skip_trailer_backward()
{
	return fseek(fh, 4, SEEK_CUR) == 0;
}

void tm_11::write_word(const uint16_t addr, uint16_t v)
{
	DOLOG(debug, false, "TM-11 write %o: %o", addr, v);

	if (addr == TM_11_MTC) {
		if (v & 1) { // GO
			const int func   = (v >> 1) & 7; // FUNCTION
			bool      ok     = true;
			uint16_t  reclen = -registers[(TM_11_MTBRC - TM_11_BASE) / 2] * 2;

			DOLOG(debug, false, "TM-11 invoke %d", func);

			if (func == 0) { // off-line
			}
			else if (func == 1) { // read
				DOLOG(debug, false, "TM-11 read of %u bytes requested", reclen);
				auto    length = find_data_record_forward();
				if (length.has_value() == false || reclen > sizeof(xfer_buffer))
					ok = false;
				else {
					uint32_t mem_offset  = registers[(TM_11_MTCMA - TM_11_BASE) / 2];
					unsigned will_read_n = std::min(unsigned(reclen), length.value());
					DOLOG(debug, false, "reading %d bytes from offset %ld to %06o", will_read_n, ftell(fh), mem_offset);
					if (ok && fread(xfer_buffer, 1, will_read_n, fh) != will_read_n)
						ok = false;
					if (ok && reclen < length && fseek(fh, length.value() - will_read_n, SEEK_CUR) != 0)
						ok = false;
					if (ok) {
						for(unsigned i=0; i<will_read_n; i++)
							m->write_byte(mem_offset + i, xfer_buffer[i]);
					}

					skip_trailer_forward();
				}
			}
			else if (func == 2) { // write
				DOLOG(debug, false, "TM-11 write of %u bytes requested", reclen);
				auto    length = find_data_record_backward();
				if (length.has_value() == false || reclen > sizeof(xfer_buffer))
					ok = false;
				else {
					uint32_t mem_offset   = registers[(TM_11_MTCMA - TM_11_BASE) / 2];
					unsigned will_write_n = std::min(unsigned(reclen), length.value());
					DOLOG(debug, false, "writing %d bytes to offset %ld from %06o", will_write_n, ftell(fh), mem_offset);
					for(int i=0; i<reclen; i++)
						xfer_buffer[i] = m->read_byte(mem_offset + i);
					if (ok && fwrite(xfer_buffer, 1, will_write_n, fh) != will_write_n)
						ok = false;

					skip_trailer_forward();
				}
			}
			else if (func == 4) { // space forward
				auto length = find_data_record_forward();
				if (length.has_value()) {
					// including trailer
					if (fseek(fh, length.value() + 4, SEEK_CUR) != 0)
						ok = false;
				}
				else {
					ok = false;
				}
			}
			else if (func == 5) { // space backward
				auto length = find_data_record_backward();
				if (length.has_value()) {
					// including header
					if (fseek(fh, -(length.value() + 4), SEEK_CUR) != 0)
						ok = false;
				}
				else {
					ok = false;
				}
			}
			else if (func == 7) { // rewind
				if (fseek(fh, 0, SEEK_SET) != 0)
					ok = false;
			}

			if (v & 64)  // interrupt enabled
				b->getCpu()->queue_interrupt(5, 0224);

			v = ok ? 128 : 32768;
		}
		else {
			if ((v & 0101) == 0100)  // IE, no GO also triggers an interrupt
				b->getCpu()->queue_interrupt(5, 0224);
		}
	}
	else if (addr == TM_11_MTCMA) {
		v &= ~1;
		DOLOG(debug, false, "Set DMA address to %o", v);
	}

	DOLOG(debug, false, "set register %o to %o", addr, v);
	registers[(addr - TM_11_BASE) / 2] = v;
}
