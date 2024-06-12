// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <optional>
#include <stdint.h>
#include <string>

#include "bus.h"


typedef enum { BL_NONE, BL_RK05, BL_RL02, BL_RP06 } bootloader_t;

void                    loadbin(bus *const b, uint16_t base, const char *const file);
void                    set_boot_loader(bus *const b, const bootloader_t which);
std::optional<uint16_t> load_tape(bus *const b, const std::string & file);
void                    load_p11_x11(bus *const b, const std::string & file);
