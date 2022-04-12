#include "bus.h"
#include "console.h"

void debugger(console *const cnsl, bus *const b, std::atomic_bool *const interrupt_emulation, const bool tracing);
