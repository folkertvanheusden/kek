// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#pragma once

#if defined(ESP32)
#include <Arduino.h>

#include <SPI.h>

#define USE_SDFAT
#define SD_FAT_TYPE 1
#include <SdFat.h>
#endif
