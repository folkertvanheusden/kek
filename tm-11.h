// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>

#define TM_11_MTS	0772520	// status register
#define TM_11_MTC	0772522	// command register
#define TM_11_MTBRC	0772524	// byte record counter
#define TM_11_MTCMA	0772526	// current memory address register
#define TM_11_MTD	0772530	// data buffer register
#define TM_11_MTRD	0772532	// TU10 read lines
#define TM_11_BASE	TM_11_MTS
#define TM_11_END	(TM_11_MTRD + 2)

class memory;

class tm_11
{
private:
	memory *const m;
	uint16_t registers[6];
	uint8_t xfer_buffer[65536];
	int offset;
	FILE *fh;

public:
	tm_11(const std::string & file, memory *const m);
	virtual ~tm_11();

	uint8_t readByte(const uint16_t addr);
	uint16_t readWord(const uint16_t addr);

	void writeByte(const uint16_t addr, const uint8_t v);
	void writeWord(const uint16_t addr, uint16_t v);
};
