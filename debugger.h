// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include "bus.h"
#include "console.h"
#include "gen.h"


std::optional<std::tuple<std::vector<disk_backend *>, std::vector<disk_backend *>, std::string> > load_disk_configuration(console *const c);
bool save_disk_configuration(const std::string & nbd_host, const int nbd_port, const disk_type_t dt);
void set_disk_configuration(bus *const b, console *const cnsl, std::tuple<std::vector<disk_backend *>, std::vector<disk_backend *>, std::string> & disk_files);

int disassemble(cpu *const c, console *const cnsl, const uint16_t pc, const bool instruction_only);
void debugger(console *const cnsl, bus *const b, std::atomic_uint32_t *const stop_event, const bool tracing);

void run_bic(console *const cnsl, bus *const b, std::atomic_uint32_t *const stop_event, const bool tracing, const uint16_t bic_start);
