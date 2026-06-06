// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include <optional>
#include <stdint.h>
#include <string>

#include "bus.h"


class console;

typedef enum { BL_NONE, BL_RK05, BL_RL02, BL_RP06, BL_TM11 } bootloader_t;

void                    loadbin(bus *const b, uint16_t base, const char *const file);
std::optional<uint16_t> set_boot_loader(bus *const b, const bootloader_t which);
std::optional<uint16_t> load_tape(bus *const b, const std::string & file, console *const cnsl);
