// (C) 2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <errno.h>
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "error.h"
#include "gen.h"
#include "log.h"
#include "rl02.h"
#include "utils.h"


constexpr int sectors_per_track = 40;
constexpr int bytes_per_sector  = 256;

static const char * const regnames[] = { 
	"control status",
	"bus address   ",
	"disk address  ",
	"multipurpose  "
	};

rl02::rl02(const std::vector<std::string> & files, bus *const b, std::atomic_bool *const disk_read_acitivity, std::atomic_bool *const disk_write_acitivity) :
	b(b),
	disk_read_acitivity(disk_read_acitivity),
	disk_write_acitivity(disk_write_acitivity)
{
	memset(registers, 0x00, sizeof registers);
	memset(xfer_buffer, 0x00, sizeof xfer_buffer);

	for(auto file : files) {
#if defined(ESP32)
		File32 *fh = new File32();

		if (!fh->open(file.c_str(), O_RDWR)) {
			delete fh;
			error_exit(true, "rl02: cannot open \"%s\"", file.c_str());
		}
#else
		FILE *fh = fopen(file.c_str(), "rb");
		if (!fh)
			error_exit(true, "rl02: cannot open \"%s\"", file.c_str());
#endif

		fhs.push_back(fh);
	}
}

rl02::~rl02()
{
	for(auto fh : fhs) {
#if defined(ESP32)
		fh->close();
		delete fh;
#else
		fclose(fh);
#endif
	}
}

uint8_t rl02::readByte(const uint32_t addr)
{
	uint16_t v = readWord(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t rl02::readWord(const uint32_t addr)
{
	const int reg = (addr - RL02_BASE) / 2;

	if (addr == RL02_CSR) {  // control status
		setBit(registers[reg], 0, true);  // drive ready (DRDY)
		setBit(registers[reg], 7, true);  // controller ready (CRDY)
	}

	uint16_t value = registers[reg];

	// TODO

	DOLOG(debug, true, "RL02 read %s/%o: %06o", regnames[reg], addr, value);

	return value;
}

void rl02::writeByte(const uint32_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - RL02_BASE) / 2];

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

uint32_t rl02::calcOffset(const uint16_t da)
{
	int           sector            = da & 63;
	int           track             = (da >> 6) & 1023;

	uint32_t      offset            = (sectors_per_track * track + sector) * bytes_per_sector;

	return offset;
}

void rl02::writeWord(const uint32_t addr, uint16_t v)
{
#if defined(ESP32)
	digitalWrite(LED_BUILTIN, LOW);
#endif

	DOLOG(debug, true, "RL02 write %06o: %06o", addr, v);

	const int reg = (addr - RL02_BASE) / 2;

        registers[reg] = v;

	if (addr == RL02_CSR) {  // control status
		const uint8_t command = (v >> 1) & 7;

		const bool    do_exec = !(v & 128);

		DOLOG(debug, true, "RL02 set command %d, exec: %d", command, do_exec);

		uint32_t disk_offset = calcOffset(registers[(RL02_DAR - RL02_BASE) / 2] & ~1);
		int      device      = 0;  // TODO

		if (command == 2) {  // get status
			registers[(RL02_MPR - RL02_BASE) / 2] = 0;
#if 0
			if (registers[(RL02_DAR - RL02_BASE) / 2] & 2) {  // get status -> load status word in MPR
				registers[(RL02_MPR - RL02_BASE) / 2] = 5 |  // lock on
					(1 << 3) |  // brushes are home
					(1 << 7) |  // this is an RL02
					0;

				DOLOG(debug, true, "RL02 set MPR for status to %06o", registers[(RL02_MPR - RL02_BASE) / 2]);
			}
#endif
		}
		else if (command == 6 || command == 7) {  // read data / read data without header check
			*disk_read_acitivity = true;

			bool proceed = true;

#if defined(ESP32)
			File32 *fh = fhs.at(device);

			if (!fh->seek(disk_offset)) {
				DOLOG(ll_error, true, "RL02 seek to %d error %s", disk_offset, strerror(errno));

				proceed = false;
			}
#else
			FILE *fh = nullptr;

			if (size_t(device) >= fhs.size())
				proceed = false;
			else {
				fh = fhs.at(device);

				if (fseek(fh, disk_offset, SEEK_SET) == -1) {
					DOLOG(ll_error, true, "RL02 seek error %s", strerror(errno));
					proceed = false;
				}
			}
#endif

			uint32_t memory_address = registers[(RL02_BAR - RL02_BASE) / 2];

			uint32_t count          = (65536 - registers[(RL02_MPR - RL02_BASE) / 2]) * 2;

			DOLOG(debug, true, "RL02 read %d bytes (dec) from %d (dec) to %06o (oct)", count, disk_offset, memory_address);

			uint32_t p    = memory_address;
			while(proceed && count > 0) {
				uint32_t cur = std::min(uint32_t(sizeof xfer_buffer), count);

#if defined(ESP32)
				yield();

				if (fh->read(xfer_buffer, cur) != size_t(cur))
					DOLOG(info, true, "RL02 fread error: %s", strerror(errno));
#else
				if (fread(xfer_buffer, 1, cur, fh) != size_t(cur))
					DOLOG(info, true, "RL02 fread error: %s", strerror(errno));
#endif

				for(uint32_t i=0; i<cur; i++, p++)
					b->writeByte(p, xfer_buffer[i]);

				count -= cur;
			}

			if (registers[(RL02_CSR - RL02_BASE) / 2] & 64) {  // interrupt enable?
				DOLOG(debug, true, "RL02 triggering interrupt");

				b->getCpu()->queue_interrupt(5, 0254);
			}

			*disk_read_acitivity = false;
		}
	}

#if defined(ESP32)
	digitalWrite(LED_BUILTIN, HIGH);
#endif
}
