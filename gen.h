// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#pragma once

extern bool trace_output;

typedef enum { EVENT_NONE = 0, EVENT_HALT, EVENT_INTERRUPT, EVENT_TERMINATE } stop_event_t;

#if defined(ESP32)
#define D(...) do { } while(0);
#else
#ifndef NDEBUG
#define D(x) do { if (trace_output) { x } } while(0);
#else
#define D(...) do { } while(0);
#endif
#endif
