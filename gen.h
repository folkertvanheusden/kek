// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

typedef enum { EVENT_NONE = 0, EVENT_HALT, EVENT_INTERRUPT, EVENT_TERMINATE } stop_event_t;

typedef enum { DT_RK05, DT_RL02, DT_TAPE } disk_type_t;

typedef enum { d_space, i_space } d_i_space_t;

typedef enum { wm_word = 0, wm_byte = 1 } word_mode_t;

typedef enum { rm_prev, rm_cur } rm_selection_t;

#if (defined(linux) || defined (__unix__) || (defined (__APPLE__) && defined (__MACH__)))
#define IS_POSIX 1
#else
#define IS_POSIX 0
#endif

#if IS_POSIX
#include <jansson.h>
#endif

#if defined(ESP32) || defined(BUILD_FOR_RP2040)
// ESP32 goes in a crash-loop when allocating 128kB
// see also https://github.com/espressif/esp-idf/issues/1934
#define DEFAULT_N_PAGES 12
#else
#define DEFAULT_N_PAGES 31
#endif
