// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

typedef enum { EVENT_NONE = 0, EVENT_HALT, EVENT_INTERRUPT, EVENT_TERMINATE } stop_event_t;

typedef enum { DT_RK05, DT_RL02, DT_TAPE } disk_type_t;

typedef enum { d_space, i_space } d_i_space_t;

typedef enum { wm_word = 0, wm_byte = 1 } word_mode_t;

typedef enum { rm_prev, rm_cur } rm_selection_t;
