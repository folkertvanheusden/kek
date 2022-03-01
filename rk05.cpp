// (C) 2018 by Folkert van Heusden
// Released under AGPL v3.0
#include <errno.h>
#include <string.h>

#include "rk05.h"
#include "gen.h"
#include "bus.h"
#include "utils.h"

const char * const regnames[] = { 
	"RK05_DS drivestatus",
	"RK05_ERROR        ",
	"RK05_CS ctrlstatus",
	"RK05_WC word count",
	"RK05_BA busaddress",
	"RK05_DA disk addrs",
	"RK05_DATABUF      "
	};

rk05::rk05(const std::string & file, bus *const b) : b(b)
{
	memset(registers, 0x00, sizeof registers);
	memset(xfer_buffer, 0x00, sizeof xfer_buffer);

	fh = fopen(file.c_str(), "rb");
}

rk05::~rk05()
{
	fclose(fh);
}

uint8_t rk05::readByte(const uint16_t addr)
{
	uint16_t v = readWord(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t rk05::readWord(const uint16_t addr)
{
	const int reg = (addr - RK05_BASE) / 2;

	fprintf(stderr, "RK05 read %s/%o: ", reg[regnames], addr);

	if (addr == RK05_DS) {		// 0177400
		setBit(registers[reg], 11, true); // disk on-line
		setBit(registers[reg], 8, true); // sector ok
		setBit(registers[reg], 7, true); // drive ready
		setBit(registers[reg], 6, true); // seek ready
		setBit(registers[reg], 4, true); // heads in position
	}
	else if (addr == RK05_ERROR)	// 0177402
		registers[reg] = 0;
	else if (addr == RK05_CS) {	// 0177404
		setBit(registers[reg], 15, false); // clear error
		setBit(registers[reg], 14, false); // clear hard error
#if 0
		if (registers[reg] & 1)
			registers[reg] &= ~128; // controller ready
		else
			registers[reg] |= 128; // controller ready
#else
		setBit(registers[reg], 7, true); // controller ready
#endif
	}

	uint16_t vtemp = registers[reg];

	if (addr == RK05_CS)
		setBit(registers[reg], 0, false); // clear go

	fprintf(stderr, "%o\n", vtemp);

	return vtemp;
}

void rk05::writeByte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - RK05_BASE) / 2];

	if (addr & 1) {
		vtemp &= ~0xff00;
		vtemp |= v << 8;
	}
	else {
		vtemp &= ~0x00ff;
		vtemp |= v;
	}

	writeWord(addr, vtemp);
}

void rk05::writeWord(const uint16_t addr, uint16_t v)
{
	const int reg = (addr - RK05_BASE) / 2;
	fprintf(stderr, "RK05 write %s/%o: %o\n", regnames[reg], addr, v);

	if (addr == RK05_CS) {
		if (v & 1) { // GO
			const int func = (v >> 1) & 7; // FUNCTION
			int16_t wc = registers[(RK05_WC - RK05_BASE) / 2];
			const size_t reclen = wc < 0 ? (-wc * 2) : wc * 2;
			fprintf(stderr, "RK05 rec len %zd\n", reclen);

			uint16_t dummy = registers[(RK05_DA - RK05_BASE) / 2];
			uint8_t sector = dummy & 0b1111;
			uint8_t surface = (dummy >> 4) & 1;
			int track = (dummy >> 4) & 511;
			uint16_t cylinder = (dummy >> 5) & 255;
			fprintf(stderr, "RK05 position sec %d surf %d cyl %d\n", sector, surface, cylinder);

			const int diskoff = track * 12 + sector;

			const int diskoffb = diskoff * 512; // RK05 is high density
			const uint16_t memoff = registers[(RK05_BA - RK05_BASE) / 2];

			fprintf(stderr, "invoke %d\n", func);

			if (func == 0) { // controller reset
			}
			else if (func == 1) { // write
				D(fprintf(stderr, "RK05 writing %zo bytes to offset %o (%d dec)\n", reclen, diskoffb, diskoffb);)

				int p = reclen; // FIXME
				for(size_t i=0; i<reclen; i++)
					xfer_buffer[i] = b -> readByte(memoff + i);

				if (fseek(fh, diskoffb, SEEK_SET) == -1)
					fprintf(stderr, "RK05 seek error %s\n", strerror(errno));
				if (fwrite(xfer_buffer, 1, reclen, fh) != reclen)
					fprintf(stderr, "RK05 fwrite error %s\n", strerror(errno));

				if (v & 2048)
					fprintf(stderr, "RK05 inhibit BA increase\n");
				else
					registers[(RK05_BA - RK05_BASE) / 2] += p;

				if (++sector >= 12) {
					sector = 0;
					if (++surface >= 2) {
						surface = 0;
						cylinder++;
					}
				}

				registers[(RK05_DA - RK05_BASE) / 2] = sector | (surface << 4) | (cylinder << 5);
			}
			else if (func == 2) { // read
				fprintf(stderr, "RK05 reading %zo bytes from offset %o (%d dec) to %o\n", reclen, diskoffb, diskoffb, memoff);

				if (fseek(fh, diskoffb, SEEK_SET) == -1)
					fprintf(stderr, "RK05 seek error %s\n", strerror(errno));
				
				int temp = reclen;
				int p = memoff;
				while(temp > 0) {
					int cur = std::min(int(sizeof xfer_buffer), temp);

					if (fread(xfer_buffer, 1, cur, fh) != size_t(cur))
						D(fprintf(stderr, "RK05 fread error: %s\n", strerror(errno));)

					for(int i=0; i<cur; i++) {
						if (p < 0160000)
							b -> writeByte(p, xfer_buffer[i]);
						p++;
					}

					temp -= cur;
				}

				if (v & 2048)
					fprintf(stderr, "RK05 inhibit BA increase\n");
				else
					registers[(RK05_BA - RK05_BASE) / 2] += p;

				if (++sector >= 12) {
					sector = 0;
					if (++surface >= 2) {
						surface = 0;
						cylinder++;
					}
				}
				registers[(RK05_DA - RK05_BASE) / 2] = sector | (surface << 4) | (cylinder << 5);
			}
			else if (func == 4) {
				fprintf(stderr, "RK05 seek to offset %o\n", diskoffb);
			}
			else if (func == 7)
				fprintf(stderr, "RK05 write lock\n");
			else {
				fprintf(stderr, "RK05 command %d UNHANDLED\n", func);
			}

			if (v & 64) { // bit 6, invoke interrupt when done vector address 220, see http://www.pdp-11.nl/peripherals/disk/rk05-info.html
				fprintf(stderr, "RK05 HIER\n"); // FIXME
			}

			registers[(RK05_WC - RK05_BASE) / 2] = 0;
		}
	}

	D(fprintf(stderr, "set register %o to %o\n", addr, v);)
	registers[reg] = v;
}
