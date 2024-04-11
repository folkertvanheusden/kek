// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#pragma once

typedef enum { EVENT_NONE = 0, EVENT_HALT, EVENT_INTERRUPT, EVENT_TERMINATE } stop_event_t;

typedef enum { DT_RK05, DT_RL02, DT_TAPE } disk_type_t;
