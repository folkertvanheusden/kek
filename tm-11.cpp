// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <errno.h>
#include <string.h>

#include "tm-11.h"
#include "gen.h"
#include "log.h"
#include "memory.h"
#include "utils.h"

tm_11::tm_11(bus *const b): m(b->getRAM())
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

	reset();
}

void tm_11::load(const std::string & file)
{
	if (fh)
		fclose(fh);

	fh = fopen(file.c_str(), "rb");
	tape_file = file;

	reset();
}

void tm_11::show_state(console *const cnsl) const
{
	cnsl->put_string_lf(format("MTS   : %06o", registers[0]));
	cnsl->put_string_lf(format("MTC   : %06o", registers[1]));
	cnsl->put_string_lf(format("MTBRC : %06o", registers[2]));
	cnsl->put_string_lf(format("MTCMA : %06o", registers[3]));
	cnsl->put_string_lf(format("MTD   : %06o", registers[4]));
	cnsl->put_string_lf(format("MTRD  : %06o", registers[5]));
	cnsl->put_string_lf(format("offset: %d",   offset      ));
	cnsl->put_string_lf(format("tape file: %s", tape_file.c_str()));
}

void tm_11::reset()
{
	memset(registers,   0x00, sizeof registers  );
	memset(xfer_buffer, 0x00, sizeof xfer_buffer);
	offset = 0;
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
		vtemp = 0;
	}

	TRACE("TM-11 read addr %o: %o", addr, vtemp);

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

void tm_11::write_word(const uint16_t addr, uint16_t v)
{
	TRACE("TM-11 write %o: %o", addr, v);

	if (addr == TM_11_MTC) {
		if (v & 1) { // GO
			const int func = (v >> 1) & 7; // FUNCTION
			const int reclen = 512;

			TRACE("invoke %d", func);

			if (func == 0) { // off-line
				v = 128; // TODO set error if error
			}
			else if (func == 1) { // read
				TRACE("reading %d bytes from offset %d", reclen, offset);
				if (fread(xfer_buffer, 1, reclen, fh) != reclen)
					DOLOG(info, true, "failed: %s", strerror(errno));
				for(int i=0; i<reclen; i++)
					m->write_byte(registers[(TM_11_MTCMA - TM_11_BASE) / 2] + i, xfer_buffer[i]);
				offset += reclen;

				v = 128; // TODO set error if error
			}
			else if (func == 2) { // write
				for(int i=0; i<reclen; i++)
					xfer_buffer[i] = m->read_byte(registers[(TM_11_MTCMA - TM_11_BASE) / 2] + i);
				fwrite(xfer_buffer, 1, reclen, fh);
				offset += reclen;
				v = 128; // TODO
			}
			else if (func == 4) { // space forward
				offset += reclen;
				v = 128; // TODO
			}
			else if (func == 5) { // space backward
				if (offset >= reclen)
					offset -= reclen;
				v = 128; // TODO
			}
			else if (func == 7) { // rewind
				offset = 0;
				v = 128; // TODO set error if error
			}
		}
	}
	else if (addr == TM_11_MTCMA) {
		v &= ~1;
		TRACE("Set DMA address to %o", v);
	}

	TRACE("set register %o to %o", addr, v);
	registers[(addr - TM_11_BASE) / 2] = v;
}
