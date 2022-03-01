// (C) 2018 by Folkert van Heusden
// Released under AGPL v3.0
#include <stdint.h>
#include <string>

void setBit(uint16_t & v, const int bit, const bool vb);
int parity(int v);

#define sign(a) ( ( (a) < 0 )  ?  -1   : ( (a) > 0 ) )

std::string format(const char *const fmt, ...);
unsigned long get_ms();
