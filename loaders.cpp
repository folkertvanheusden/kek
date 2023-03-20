#include <stdint.h>
#include <stdio.h>
#include <string>

#include "bus.h"
#include "cpu.h"
#include "error.h"
#include "gen.h"
#include "loaders.h"
#include "log.h"
#include "utils.h"


void loadbin(bus *const b, uint16_t base, const char *const file)
{
	FILE *fh = fopen(file, "rb");

	while(!feof(fh))
		b -> writeByte(base++, fgetc(fh));

	fclose(fh);
}

void setBootLoader(bus *const b, const bootloader_t which)
{
	cpu *const c      = b -> getCpu();

	uint16_t         offset = 0;
	uint16_t         start  = 0;
	const uint16_t  *bl     = nullptr;
	int              size   = 0;

	if (which == BL_RK05) {
		start = offset = 01000;

		static uint16_t rk05_code[] = {
			0012700,
			0177406,
			0012710,
			0177400,
			0012740,
			0000005,
			0105710,
			0100376,
			0005007
		};

#if 0
		// from https://github.com/amakukha/PyPDP11.git
		offset = 02000;
		start  = 02002;

		static uint16_t rk05_code[] = {
			0042113,                        // "KD"
			0012706, 02000,                // MOV #boot_start, SP
			0012700, 0000000,              // MOV #unit, R0        ; unit number
			0010003,                        // MOV R0, R3
			0000303,                        // SWAB R3
			0006303,                        // ASL R3
			0006303,                        // ASL R3
			0006303,                        // ASL R3
			0006303,                        // ASL R3
			0006303,                        // ASL R3
			0012701, 0177412,              // MOV #RKDA, R1        ; csr
			0010311,                        // MOV R3, (R1)         ; load da
			0005041,                        // CLR -(R1)            ; clear ba
			0012741, 0177000,              // MOV #-256.*2, -(R1)  ; load wc
			0012741, 0000005,              // MOV #READ+GO, -(R1)  ; read & go
			0005002,                        // CLR R2
			0005003,                        // CLR R3
			0012704, 02020,                // MOV #START+20, R4
			0005005,                        // CLR R5
			0105711,                        // TSTB (R1)
			0100376,                        // BPL .-2
			0105011,                        // CLRB (R1)
			0005007                         // CLR PC
		};
#endif

		bl = rk05_code;

		size = sizeof(rk05_code)/sizeof(rk05_code[0]);
	}
	else if (which == BL_RL02) {
		start = offset = 01000;

		/* from https://www.pdp-11.nl/peripherals/disk/rl-info.html
		static uint16_t rl02_code[] = {
			0012701,
			0174400,
			0012761,
			0000013,
			0000004,
			0012711,
			0000004,
			0105711,
			0100376,
			0005061,
			0000002,
			0005061,
			0000004,
			0012761,
			0177400,
			0000006,
			0012711,
			0000014,
			0105711,
			0100376,
			0005007
		};

		size = 21;
		*/

		// from http://gunkies.org/wiki/RL11_disk_controller
		static uint16_t rl02_code[] = {
			0012700,
			0174400,
			0012760,
			0177400,
			0000006,
			0012710,
			0000014,
			0105710,
			0100376,
			0005007,
		};

		size = sizeof(rl02_code)/sizeof(rl02_code[0]);

		bl = rl02_code;
	}

	for(int i=0; i<size; i++)
		b -> writeWord(offset + i * 2, bl[i]);

	c -> setRegister(7, start);
}

uint16_t loadTape(bus *const b, const std::string & file)
{
	FILE *fh = fopen(file.c_str(), "rb");
	if (!fh) {
		DOLOG(ll_error, true, "Cannot open %s", file.c_str());
		return -1;
	}

	uint16_t start = 0, end = 0;

	for(;!feof(fh);) {
		uint8_t buffer[6];

		if (fread(buffer, 1, 6, fh) != 6)
			break;

		int count = (buffer[3] << 8) | buffer[2];
		int p = (buffer[5] << 8) | buffer[4];

		uint8_t csum = 0;
		for(int i=2; i<6; i++)
			csum += buffer[i];

		if (count == 6) { // eg no data
			if (p != 1) {
				DOLOG(info, true, "Setting start address to %o", p);
				start = p;
			}
		}

		DOLOG(debug, true, "%ld] reading %d (dec) bytes to %o (oct)", ftell(fh), count - 6, p);

		for(int i=0; i<count - 6; i++) {
			if (feof(fh)) {
				DOLOG(warning, true, "short read");
				break;
			}
			uint8_t c = fgetc(fh);

			csum += c;
			b -> writeByte(p++, c);

			if (p > end)
				end = p;
		}

		int fcs = fgetc(fh);
		csum += fcs;

		if (csum != 255)
			DOLOG(warning, true, "checksum error %d", csum);
	}

	fclose(fh);

	return start;
}

void load_p11_x11(bus *const b, const std::string & file)
{
	FILE *fh = fopen(file.c_str(), "rb");
	if (!fh)
		error_exit(true, "Cannot open %s", file.c_str());

	uint16_t addr    = 0;
	int      n = 0;

	while(!feof(fh)) {
		char buffer[4096];

		if (!fgets(buffer, sizeof buffer, fh))
			break;

		if (n) {
			uint8_t byte = strtol(buffer, nullptr, 16);

			b->writeByte(addr, byte);

			n--;

			addr++;
		}
		else {
			std::vector<std::string> parts = split(buffer, " ");

			addr = strtol(parts[0].c_str(), nullptr, 16);
			n    = strtol(parts[1].c_str(), nullptr, 16);
		}
	}

	fclose(fh);

	cpu *const c      = b -> getCpu();
	c -> setRegister(7, 0);
}
