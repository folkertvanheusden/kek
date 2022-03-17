// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>

#if defined(ESP32)
#include <SPI.h>
#define USE_SDFAT
#define SD_FAT_TYPE 1
#include <SdFat.h>
#endif

// FIXME namen van defines
#define RK05_DS		0177400	// drive status
#define RK05_ERROR	0177402 // error
#define RK05_CS		0177404 // control status
#define RK05_WC		0177406 // word count
#define RK05_BA		0177410 // bus address
#define RK05_DA		0177412 // disk address
#define RK05_DATABUF	0177414 // data buffer
#define RK05_BASE	RK05_DS
#define RK05_END	(RK05_DATABUF + 2)

class bus;

class rk05
{
private:
	bus *const b;
	uint16_t registers[7];
	uint8_t xfer_buffer[512];
#if defined(ESP32)
	SdFat32 sd;
	File32 fh;
#else
	FILE *fh;
#endif

public:
	rk05(const std::string & file, bus *const b);
	virtual ~rk05();

	uint8_t readByte(const uint16_t addr);
	uint16_t readWord(const uint16_t addr);

	void writeByte(const uint16_t addr, const uint8_t v);
	void writeWord(const uint16_t addr, uint16_t v);
};
