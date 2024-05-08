// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

#if defined(ESP32)
#include <Arduino.h>

#include <SPI.h>

#define USE_SDFAT
#define SD_FAT_TYPE 1
#include <SdFat.h>
#endif

// #define NEOPIXELS_PIN 24

// #define HEARTBEAT_PIN LED_BUILTIN
#define HEARTBEAT_PIN 25

// #define TTY_SERIAL_RX 16
// #define TTY_SERIAL_TX 17
