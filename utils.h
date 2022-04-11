// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <stdint.h>
#include <string>
#include <vector>

void setBit(uint16_t & v, const int bit, const bool vb);
int parity(int v);

#define sign(a) ( ( (a) < 0 )  ?  -1   : ( (a) > 0 ) )

std::string format(const char *const fmt, ...);

std::vector<std::string> split(std::string in, std::string splitter);

unsigned long get_ms();
void myusleep(uint64_t us);

void set_thread_name(std::string name);
