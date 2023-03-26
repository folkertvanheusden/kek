#include "bus.h"
#include "console.h"

void debugger(console *const cnsl, bus *const b, std::atomic_uint32_t *const stop_event, const bool tracing);
void run_bic(console *const cnsl, bus *const b, std::atomic_uint32_t *const stop_event, const bool tracing, const uint16_t bic_start);
