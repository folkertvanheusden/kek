// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

typedef enum { EVENT_NONE = 0, EVENT_HALT, EVENT_INTERRUPT, EVENT_TERMINATE } stop_event_t;

typedef enum { WM_WORD = 0, WM_BYTE = 1 } word_mode_t;

typedef enum { RM_CUR, RM_PREV } run_mode_sel_t;
