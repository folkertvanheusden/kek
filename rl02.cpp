// (C) 2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <errno.h>
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "error.h"
#include "gen.h"
#include "rl02.h"
#include "utils.h"


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

uint8_t rl02::readByte(const uint16_t addr)
{
	uint16_t v = readWord(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t rl02::readWord(const uint16_t addr)
{
	const int reg = (addr - RK05_BASE) / 2;

	uint16_t value = 0;

	// TODO

	D(fprintf(stderr, "RK05 read %s/%o: %06o\n", reg[regnames], addr, value);)

	return value;
}

void rl02::writeByte(const uint16_t addr, const uint8_t v)
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

void rl02::writeWord(const uint16_t addr, uint16_t v)
{
#if defined(ESP32)
	digitalWrite(LED_BUILTIN, LOW);
#endif


#if defined(ESP32)
	digitalWrite(LED_BUILTIN, HIGH);
#endif
}
