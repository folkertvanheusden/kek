// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "bus.h"
#include "console.h"
#include "gen.h"


std::tuple<int, uint32_t, bool, std::string> disassemble(cpu *const c, console *const cnsl, const uint16_t pc, const bool instruction_only);
void debugger(console *const cnsl, bus *const b, kek_event_t *const stop_event, const std::optional<std::string> & init);

void simple_run(console *const cnsl, bus *const b, kek_event_t *const stop_event);
