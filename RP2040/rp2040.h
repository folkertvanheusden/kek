#pragma once

#if defined(RP2040)
#include <Arduino.h>

#include <SPI.h>

#define USE_SDFAT
#define SD_FAT_TYPE 1
#include <SdFat.h>
#endif
