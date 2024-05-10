// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <stdint.h>
#include <string>
#include <vector>

void setBit(uint16_t & v, const int bit, const bool vb);
int parity(int v);

#define sign(a) ( ( (a) < 0 )  ?  -1   : ( (a) > 0 ) )

std::string format(const char *const fmt, ...);

std::vector<std::string> split(std::string in, std::string splitter);

unsigned long get_ms();
uint64_t get_us();
void myusleep(uint64_t us);

std::string get_thread_name();
void set_thread_name(std::string name);

ssize_t WRITE(int fd, const char *whereto, size_t len);
ssize_t READ(int fd, char *whereto, size_t len);

void update_word(uint16_t *const w, const bool msb, const uint8_t v);

void set_nodelay(const int fd);
std::string get_endpoint_name(const int fd);
