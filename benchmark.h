#include <atomic>

#include "bus.h"


void benchmark(console *const cnsl, bus *const b, std::atomic_uint32_t *const event, const bool measure);
