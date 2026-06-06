// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#if !defined(_WIN32)
#include <ArduinoJson.h>
#endif
#include <optional>
#include <stdint.h>
#include <string>
#include <vector>

void setBit(uint16_t & v, const int bit, const bool vb);
int parity(int v);

#define sign(a) ( ( (a) < 0 )  ?  -1   : ( (a) > 0 ) )

std::string format(const char *const fmt, ...);

std::vector<std::string> split(std::string in, std::string splitter);

uint64_t get_ms();
uint64_t get_us();
void myusleep(uint64_t us);

std::string get_thread_name();
void set_thread_name(std::string name);

ssize_t WRITE(int fd, const char *whereto, size_t len);
ssize_t READ(int fd, char *whereto, size_t len);

void update_word(uint16_t *const w, const bool msb, const uint8_t v);

void set_nodelay(const int fd);
std::string get_endpoint_name(const int fd);

#if !defined(_WIN32)
std::optional<JsonDocument> deserialize_file(const std::string & filename);
#endif

std::string get_configuration_string(const std::string & file, const std::string & default_value);
uint32_t    get_configuration_uint32(const std::string & file, const uint32_t default_value);
bool put_configuration_uint32(const std::string & file, const uint32_t value);
bool put_configuration_string(const std::string & file, const std::string & value);

bool file_exists(const std::string & file);
