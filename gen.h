// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#pragma once

// #define TURBO

typedef enum { EVENT_NONE = 0, EVENT_HALT, EVENT_INTERRUPT, EVENT_TERMINATE } stop_event_t;

typedef enum { DT_RK05, DT_RL02, DT_TAPE } disk_type_t;

typedef enum { d_space, i_space } d_i_space_t;

typedef enum { wm_word = 0, wm_byte = 1 } word_mode_t;

#if defined(FREERTOS)
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>
#endif

#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
// ESP32 goes in a crash-loop when allocating 128kB
// see also https://github.com/espressif/esp-idf/issues/1934
#define DEFAULT_N_PAGES 12  // was 10
#else
#define DEFAULT_N_PAGES 31
#endif

#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
#define SERIAL_CFG_FILE        "serial.json"
#define BLINKENLIGHTS_CFG_FILE "blinkenlights.dat"
#else
#define SERIAL_CFG_FILE        ".serial.json"
#define BLINKENLIGHTS_CFG_FILE ".blinkenlights.dat"
#endif

#if defined(ESP32)
#include "esp32.h"
#endif

#if defined(BUILD_FOR_PICO2W)
#include "pico2w.h"
#endif

#if defined(TEENSY4_1)
#include "teensy4_1.h"
#endif

#if defined(TEENSY4_1)
#define kek_event_t  volatile uint32_t
#define abool        volatile bool
#define big_acounter volatile uint64_t
#define aint         volatile int
#define PRIzd        "d"
#define PRIzu        "u"
#define PRIlu        "u"
#define load_relaxed_p(x) (*(x))
#else
#include <atomic>
#define kek_event_t  std::atomic_uint32_t
#define abool        std::atomic_bool
#define big_acounter std::atomic_uint64_t
#define aint         std::atomic_int
#define PRIzd        "zd"
#define PRIzu        "zu"
#define PRIlu        "lu"
#define load_relaxed_p(x) ((x)->load(std::memory_order_relaxed))
#endif
