// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#if defined(ESP32)
#include <Arduino.h>
#include "esp32.h"
#endif
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
		b->write_byte(base++, fgetc(fh));

	fclose(fh);
}

void set_boot_loader(bus *const b, const bootloader_t which)
{
	cpu       *const c      = b->getCpu();

	uint16_t         offset = 0;
	uint16_t         start  = 0;
	const uint16_t  *bl     = nullptr;
	int              size   = 0;

	if (which == BL_RK05) {
		DOLOG(debug, false, "Enabling RK05 bootloader");
		start = offset = 01000;

		static const uint16_t rk05_code[] = {
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

		bl = rk05_code;

		size = sizeof(rk05_code)/sizeof(rk05_code[0]);
	}
	else if (which == BL_RL02) {
		DOLOG(debug, false, "Enabling RL02 bootloader");
		start = offset = 01000;

		/* from https://www.pdp-11.nl/peripherals/disk/rl-info.html
		static const uint16_t rl02_code[] = {
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
		static const uint16_t rl02_code[] = {
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
	else if (which == BL_RP06) {
		DOLOG(debug, false, "Enabling RP06 bootloader");
		start = offset = 02000;

		static const uint16_t rp06_code[] = {
			012701, 0176700, 012700, 0176704, 012740, 0177000, 012740, 000071, 012700, 0, 000110, 000000

		};

		size = sizeof(rp06_code)/sizeof(rp06_code[0]);

		bl = rp06_code;
	}

	for(uint16_t i=0; i<size; i++)
		b->write_word(uint16_t(offset + i * 2), bl[i]);

	c->set_register(7, start);
}

std::optional<uint16_t> load_tape(bus *const b, const std::string & file)
{
#if defined(ESP32)
	File32 fh;
	if (!fh.open(file.c_str(), O_RDONLY)) {
		DOLOG(ll_error, true, "Cannot open %s", file.c_str());
		return { };
	}
#else
	FILE *fh = fopen(file.c_str(), "rb");
	if (!fh) {
		DOLOG(ll_error, true, "Cannot open %s", file.c_str());
		return { };
	}
#endif

	std::optional<uint16_t> start;

	for(;;) {
		uint8_t buffer[6];

#if defined(ESP32)
		if (fh.read(buffer, 6) != 6)
			break;
#else
		if (fread(buffer, 1, 6, fh) != 6)
			break;
#endif

		int count = (buffer[3] << 8) | buffer[2];
		int p     = (buffer[5] << 8) | buffer[4];

		uint8_t csum = 0;
		for(int i=2; i<6; i++)
			csum += buffer[i];

		if (count == 0 || p == 1)
			break;

		if (count == 6) { // eg no data
			if (p != 1) {
				DOLOG(info, true, "Setting start address to %o", p);
				start = p;
			}
		}

#if !defined(ESP32)
		TRACE("%ld] reading %d (dec) bytes to %o (oct)", ftell(fh), count - 6, p);
#endif

		for(int i=0; i<count - 6; i++) {
#if defined(ESP32)
			uint8_t c = 0;
			if (fh.read(&c, 1) != 1) {
				DOLOG(warning, true, "short read");
				break;
			}
#else
			if (feof(fh)) {
				DOLOG(warning, true, "short read");
				break;
			}

			int c = fgetc(fh);
			if (c == -1) {
				DOLOG(warning, true, "read failure");
				break;
			}
#endif

			csum += c;
			b->write_byte(p++, c);
		}

#if defined(ESP32)
		uint8_t fcs = 0;
		fh.read(&fcs, 1);
		csum += fcs;
#else
		csum += fgetc(fh);
#endif
		if (csum != 255)
			DOLOG(warning, true, "checksum error %d", csum);
	}

#if defined(ESP32)
	fh.close();
#else
	fclose(fh);
#endif

	if (start.has_value() == false)
		start = 0200;  // assume BIC file

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

			b->write_byte(addr, byte);

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

	cpu *const c      = b->getCpu();
	c->set_register(7, 0);
}
