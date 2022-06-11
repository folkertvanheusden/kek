#include <stdint.h>
#include <string>

#include "bus.h"


typedef enum { BL_NONE, BL_RK05, BL_RL02 } bootloader_t;

void loadbin(bus *const b, uint16_t base, const char *const file);
void setBootLoader(bus *const b, const bootloader_t which);
uint16_t loadTape(bus *const b, const char *const file);
void load_p11_x11(bus *const b, const std::string & file);
