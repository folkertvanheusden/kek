// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <optional>
#include "gen.h"
#if IS_POSIX
#include <dirent.h>
#include <jansson.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <Arduino.h>
#include <LittleFS.h>
#endif

#include "breakpoint_parser.h"
#include "bus.h"
#include "console.h"
#include "cpu.h"
#include "disk_backend.h"
#if IS_POSIX
#include "disk_backend_file.h"
#else
#include "disk_backend_esp32.h"
#endif
#include "disk_backend_nbd.h"
#include "loaders.h"
#include "log.h"
#include "memory.h"
#include "tty.h"
#include "utils.h"


#if defined(ESP32) || defined(BUILD_FOR_RP2040)
#if defined(ESP32)
#include "esp32.h"
#elif defined(BUILD_FOR_RP2040)
#include "rp2040.h"
#endif

void configure_network(console *const cnsl);
void check_network(console *const cnsl);
void start_network(console *const cnsl);

void set_tty_serial_speed(console *const c, const uint32_t bps);
#endif

#if !defined(BUILD_FOR_RP2040) && !defined(linux)
extern SdFs SD;
#endif

#if !defined(BUILD_FOR_RP2040)
std::optional<disk_backend *> select_nbd_server(console *const cnsl)
{
	cnsl->flush_input();

	std::string hostname = cnsl->read_line("Enter hostname (or empty to abort): ");

	if (hostname.empty())
		return { };

	std::string port_str = cnsl->read_line("Enter port number (or empty to abort): ");

	if (port_str.empty())
		return { };

	disk_backend *d = new disk_backend_nbd(hostname, atoi(port_str.c_str()));

	if (d->begin(false) == false) {
		cnsl->put_string_lf("Cannot initialize NBD client");
		delete d;
		return { };
	}

	return d;
}
#endif

// disk image files
std::optional<disk_backend *> select_disk_file(console *const c)
{
#if IS_POSIX
	c->put_string_lf("Files in current directory: ");
#else
	c->put_string_lf(format("MISO: %d", int(MISO)));
	c->put_string_lf(format("MOSI: %d", int(MOSI)));
	c->put_string_lf(format("SCK : %d", int(SCK )));
	c->put_string_lf(format("SS  : %d", int(SS  )));

	c->put_string_lf("Files on SD-card:");

#if defined(SHA2017)
	if (!SD.begin(21, SD_SCK_MHZ(10)))
		SD.initErrorHalt();
#elif !defined(BUILD_FOR_RP2040)
	if (!SD.begin(SS, SD_SCK_MHZ(15))) {
		auto err = SD.sdErrorCode();
		if (err)
			c->put_string_lf(format("SDerror: 0x%x, data: 0x%x", err, SD.sdErrorData()));
		else
			c->put_string_lf("Failed to initialize SD card");

		return { };
	}
#endif
#endif

	for(;;) {
#if defined(linux)
		DIR *dir = opendir(".");
		if (!dir) {
			c->put_string_lf("Cannot access directory");
			return { };
		}

		dirent *dr = nullptr;
		while((dr = readdir(dir))) {
			struct stat st { };

			if (stat(dr->d_name, &st) == 0)
				c->put_string_lf(format("%s\t\t%ld", dr->d_name, st.st_size));
		}

		closedir(dir);
#elif defined(BUILD_FOR_RP2040)
		File root = SD.open("/");

		for(;;) {
			auto entry = root.openNextFile();
			if (!entry)
				break;

			if (!entry.isDirectory()) {
				c->put_string(entry.name());
				c->put_string("\t\t");
				c->put_string_lf(format("%ld", entry.size()));
			}

			entry.close();
		}
#else
		SD.ls("/", LS_DATE | LS_SIZE | LS_R);
#endif

		c->flush_input();

		std::string selected_file = c->read_line("Enter filename (or empty to abort): ");

		if (selected_file.empty())
			return { };

		c->put_string("Opening file: ");
		c->put_string_lf(selected_file.c_str());

		bool can_open_file = false;

#if IS_POSIX
		struct stat st { };
		can_open_file = stat(selected_file.c_str(), &st) == 0;
#else
		File32 fh;
		can_open_file = fh.open(selected_file.c_str(), O_RDWR);
		if (can_open_file)
			fh.close();
#endif

		if (can_open_file) {
#if IS_POSIX
			disk_backend *temp = new disk_backend_file(selected_file);
#else
			disk_backend *temp = new disk_backend_esp32(selected_file);
#endif

			if (!temp->begin(false)) {
				c->put_string("Cannot use: ");
				c->put_string_lf(selected_file.c_str());

				delete temp;

				continue;
			}

			return { temp };
		}

		c->put_string_lf("open failed");
	}

	return { };
}

int wait_for_key(const std::string & title, console *const cnsl, const std::vector<char> & allowed)
{
	cnsl->put_string_lf(title);

	cnsl->put_string("> ");

	int ch = -1;
	while(ch == -1) {
		auto temp = cnsl->wait_char(500);

		if (temp.has_value()) {
			for(auto & a: allowed) {
				if (a == temp.value()) {
					ch = temp.value();
					break;
				}
			}
		}
	}

	cnsl->put_string_lf(format("%c", ch));

	return ch;
}

std::optional<disk_backend *> select_disk_backend(console *const cnsl)
{
#if defined(BUILD_FOR_RP2040)
	return select_disk_file(cnsl);
#else
	int ch = wait_for_key("1. local disk, 2. network disk (NBD), 9. abort", cnsl, { '1', '2', '9' });
	if (ch == '9')
		return { };

	if (ch == '1')
		return select_disk_file(cnsl);

	if (ch == '2')
		return select_nbd_server(cnsl);

	return { };
#endif
}

void configure_disk(bus *const b, console *const cnsl)
{
	// TODO tape
	int type_ch = wait_for_key("1. RK05, 2. RL02, 9. abort", cnsl, { '1', '2', '3', '9' });

	bootloader_t bl = BL_NONE;
	disk_device *dd = nullptr;

	if (type_ch == '1') {
		dd = b->getRK05();
		bl = BL_RK05;
	}
	else if (type_ch == '2') {
		dd = b->getRL02();
		bl = BL_RL02;
	}
	else if (type_ch == '9') {
		return;
	}

	for(;;) {
		std::vector<char> keys_allowed { '1', '2', '9' };

		auto cartridge_slots = dd->access_disk_backends();
		int slot_key = 'A';
		for(auto & slot: *cartridge_slots) {
			cnsl->put_string_lf(format(" %c. %s", slot_key, slot ? slot->get_identifier().c_str() : "-"));
			keys_allowed.push_back(slot_key);
			slot_key++;
		}

		int ch = wait_for_key("Select cartridge to setup, 1. to add a cartridge, 2. to load a bootloader or 9. to exit", cnsl, keys_allowed);
		if (ch == '9')
			break;

		if (ch == '1') {
			auto image_file = select_disk_backend(cnsl);

			if (image_file.has_value()) {
				cartridge_slots->push_back(image_file.value());

				cnsl->put_string_lf("Cartridge loaded");
			}
		}
		else if (ch == '2') {
			set_boot_loader(b, bl);

			cnsl->put_string_lf("Bootloader loaded");
		}
		else {
			int slot = ch - 'A';

			for(;;) {
				int ch_action = wait_for_key("Select cartridge action: 1. load, 2. unload, 9. exit", cnsl, { '1', '2', '9' });
				if (ch_action == '9')
					break;

				if (ch_action == '1') {
					auto image_file = select_disk_backend(cnsl);

					if (image_file.has_value()) {
						delete cartridge_slots->at(slot);
						cartridge_slots->at(slot) = image_file.value();

						cnsl->put_string_lf("Cartridge loaded");
					}
				}
				else if (ch_action == '2') {
					if (cartridge_slots->at(slot)) {
						delete cartridge_slots->at(slot);
						cartridge_slots->at(slot) = nullptr;

						cnsl->put_string_lf("Cartridge unloaded");
					}
				}
			}
		}
	}
}

// returns size of instruction (in bytes)
int disassemble(cpu *const c, console *const cnsl, const uint16_t pc, const bool instruction_only)
{
	auto data      = c->disassemble(pc);

	auto registers = data["registers"];
	auto psw       = data["psw"][0];

	std::string instruction_values;
	for(auto & iv : data["instruction-values"])
		instruction_values += (instruction_values.empty() ? "" : ",") + iv;

	std::string work_values;
	for(auto & wv : data["work-values"])
		work_values += (work_values.empty() ? "" : ",") + wv;

	std::string instruction = data["instruction-text"].at(0);

	std::string MMR0 = data["MMR0"].at(0);
	std::string MMR1 = data["MMR1"].at(0);
	std::string MMR2 = data["MMR2"].at(0);
	std::string MMR3 = data["MMR3"].at(0);

	std::string result;

	if (instruction_only)
		result = format("PC: %06o, instr: %s\t%s\t%s",
				pc,
				instruction_values.c_str(),
				instruction.c_str(),
				work_values.c_str()
				);
	else
		result = format("R0: %s, R1: %s, R2: %s, R3: %s, R4: %s, R5: %s, SP: %s, PC: %06o, PSW: %s (%s), instr: %s: %s",
				registers[0].c_str(), registers[1].c_str(), registers[2].c_str(), registers[3].c_str(), registers[4].c_str(), registers[5].c_str(),
				registers[6].c_str(), pc, 
				psw.c_str(), data["psw-value"][0].c_str(),
				instruction_values.c_str(),
				instruction.c_str()
				);

	if (cnsl)
		cnsl->put_string_lf(result);
	else
		DOLOG(debug, false, "%s", result.c_str());

	std::string sp;
	for(auto sp_val : data["sp"])
		sp += (sp.empty() ? "" : ",") + sp_val;

	DOLOG(debug, false, "SP: %s, MMR0/1/2/3: %s/%s/%s/%s", sp.c_str(), MMR0.c_str(), MMR1.c_str(), MMR2.c_str(), MMR3.c_str());

#if 0
	if (c->getPSW_runmode() == 3) {
		/*
		FILE *fh = fopen("/home/folkert/temp/ramdisk/log-kek.dat", "a+");
		fprintf(fh, "%06o", pc);
		for(auto & v: data["instruction-values"])
			fprintf(fh, " %s", v.c_str());
		fprintf(fh, "\n");
		fclose(fh);
		*/
		FILE *fh = fopen("/home/folkert/temp/ramdisk/da-kek.txt", "a+");
		fprintf(fh, "R0 %s R1 %s R2 %s R3 %s R4 %s R5 %s R6 %s R7 %06o %s\n", registers[0].c_str(), registers[1].c_str(), registers[2].c_str(), registers[3].c_str(), registers[4].c_str(), registers[5].c_str(), registers[6].c_str(), pc, instruction.c_str());
		fclose(fh);
	}
#endif

	return data["instruction-values"].size() * 2;
}

std::map<std::string, std::string> split(const std::vector<std::string> & kv_array, const std::string & splitter)
{
	std::map<std::string, std::string> out;

	for(auto & pair : kv_array) {
		auto kv = split(pair, splitter);

		if (kv.size() == 1)
			out.insert({ kv[0], "" });
		else if (kv.size() == 2)
			out.insert({ kv[0], kv[1] });
	}

	return out;
}

void dump_par_pdr(console *const cnsl, bus *const b, const uint16_t pdrs, const uint16_t pars, const std::string & name, const int state, const std::optional<int> & selection)
{
	if (state == 0 || state == 2)
		cnsl->put_string_lf(name);
	else
		cnsl->put_string_lf(format("%s DISABLED", name.c_str()));

	cnsl->put_string_lf("   PAR             PDR    LEN");

	for(int i=0; i<8; i++) {
		if (selection.has_value() && i != selection.value())
			continue;
		uint16_t par_value = b->read(pars + i * 2, wm_word, rm_cur, true);
		uint16_t pdr_value = b->read(pdrs + i * 2, wm_word, rm_cur, true);

		uint16_t pdr_len   = (((pdr_value >> 8) & 127) + 1) * 64;

		cnsl->put_string_lf(format("%d] %06o %08o %06o %04o D%d A%d", i, par_value, par_value * 64, pdr_value, pdr_len, !!(pdr_value & 8), pdr_value & 7));
	}
}

void dump_memory_contents(console *const cnsl, bus *const b, const uint16_t read_addr)
{
	cnsl->put_string_lf(format("\tMOV #%06o,R0", read_addr));
	cnsl->put_string_lf(format("\tMOV #%06o,(R0)", b->read(read_addr, wm_word, rm_cur, true)));
}

void dump_range_as_instructions(console *const cnsl, bus *const b, const uint16_t base)
{
	for(int i=0; i<8; i++)
		dump_memory_contents(cnsl, b, base + i * 2);
}

void mmu_dump(console *const cnsl, bus *const b, const bool verbose)
{
	uint16_t mmr0 = b->getMMU()->getMMR0();
	uint16_t mmr1 = b->getMMU()->getMMR1();
	uint16_t mmr2 = b->getMMU()->getMMR2();
	uint16_t mmr3 = b->getMMU()->getMMR3();

	cnsl->put_string_lf(mmr0 & 1 ? "MMU enabled" : "MMU NOT enabled");

	cnsl->put_string_lf(format("MMR0: %06o", mmr0));
	cnsl->put_string_lf(format("MMR1: %06o", mmr1));
	cnsl->put_string_lf(format("MMR2: %06o", mmr2));
	cnsl->put_string_lf(format("MMR3: %06o", mmr3));

	dump_par_pdr(cnsl, b, ADDR_PDR_SV_START,       ADDR_PAR_SV_START,       "supervisor i-space", 0, { });
	dump_par_pdr(cnsl, b, ADDR_PDR_SV_START + 020, ADDR_PAR_SV_START + 020, "supervisor d-space", 1 + (!!(mmr3 & 2)), { });

	dump_par_pdr(cnsl, b, ADDR_PDR_K_START,       ADDR_PAR_K_START,       "kernel i-space", 0, { });
	dump_par_pdr(cnsl, b, ADDR_PDR_K_START + 020, ADDR_PAR_K_START + 020, "kernel d-space", 1 + (!!(mmr3 & 4)), { });

	dump_par_pdr(cnsl, b, ADDR_PDR_U_START,       ADDR_PAR_U_START,       "user i-space", 0, { });
	dump_par_pdr(cnsl, b, ADDR_PDR_U_START + 020, ADDR_PAR_U_START + 020, "user d-space", 1 + (!!(mmr3 & 1)), { });

	if (verbose) {
		dump_range_as_instructions(cnsl, b, ADDR_PDR_SV_START);  // sv i
		dump_range_as_instructions(cnsl, b, ADDR_PDR_SV_START + 020);  // sv d
		dump_range_as_instructions(cnsl, b, ADDR_PDR_K_START);  // k i
		dump_range_as_instructions(cnsl, b, ADDR_PDR_K_START + 020);  // k d
		dump_range_as_instructions(cnsl, b, ADDR_PDR_U_START);  // u i
		dump_range_as_instructions(cnsl, b, ADDR_PDR_U_START + 020);  // u d

		dump_memory_contents(cnsl, b, ADDR_MMR0);
		dump_memory_contents(cnsl, b, ADDR_MMR1);
		dump_memory_contents(cnsl, b, ADDR_MMR2);
		dump_memory_contents(cnsl, b, ADDR_MMR3);
	}
}

const char *trap_action_to_str(const trap_action_t ta)
{
	if (ta == T_PROCEED)
		return "proceed";
	if (ta == T_ABORT_4)
		return "abort (trap 4)";
	if (ta == T_TRAP_250)
		return "trap 250";

	return "?";
}

void mmu_resolve(console *const cnsl, bus *const b, const uint16_t va)
{
	int  run_mode = b->getCpu()->getPSW_runmode();
	cnsl->put_string_lf(format("Run mode: %d, use data space: %d", run_mode, b->getMMU()->get_use_data_space(run_mode)));

	auto data     = b->calculate_physical_address(run_mode, va);

	uint16_t page_offset = va & 8191;
	cnsl->put_string_lf(format("Active page field: %d, page offset: %o (%d)", data.apf, page_offset, page_offset));
	cnsl->put_string_lf(format("Phys. addr. instruction: %08o (psw: %d)", data.physical_instruction, data.physical_instruction_is_psw));
	cnsl->put_string_lf(format("Phys. addr. data: %08o (psw: %d)", data.physical_data, data.physical_data_is_psw));

	uint16_t mmr3 = b->getMMU()->getMMR3();

	if (run_mode == 0) {
		dump_par_pdr(cnsl, b, ADDR_PDR_K_START,       ADDR_PAR_K_START,       "kernel i-space", 0, data.apf);
		dump_par_pdr(cnsl, b, ADDR_PDR_K_START + 020, ADDR_PAR_K_START + 020, "kernel d-space", 1 + (!!(mmr3 & 4)), data.apf);
	}
	else if (run_mode == 1) {
		dump_par_pdr(cnsl, b, ADDR_PDR_SV_START,       ADDR_PAR_SV_START,       "supervisor i-space", 0, data.apf);
		dump_par_pdr(cnsl, b, ADDR_PDR_SV_START + 020, ADDR_PAR_SV_START + 020, "supervisor d-space", 1 + (!!(mmr3 & 4)), data.apf);
	}
	else if (run_mode == 3) {
		dump_par_pdr(cnsl, b, ADDR_PDR_U_START,       ADDR_PAR_U_START,       "user i-space", 0, data.apf);
		dump_par_pdr(cnsl, b, ADDR_PDR_U_START + 020, ADDR_PAR_U_START + 020, "user d-space", 1 + (!!(mmr3 & 4)), data.apf);
	}

	for(int i=0; i<2; i++) {
		auto ta_i = b->get_trap_action(run_mode, false, data.apf, i);
		auto ta_d = b->get_trap_action(run_mode, true,  data.apf, i);

		cnsl->put_string_lf(format("Instruction action: %s (%s)", trap_action_to_str(ta_i.first), i ? "write" : "read"));
		cnsl->put_string_lf(format("Data action       : %s (%s)", trap_action_to_str(ta_d.first), i ? "write" : "read"));
	}
}

void reg_dump(console *const cnsl, cpu *const c)
{
	for(uint8_t set=0; set<2; set++) {
		cnsl->put_string_lf(format("Set %d, R0: %06o, R1: %06o, R2: %06o, R3: %06o, R4: %06o, R5: %06o",
						set,
						c->lowlevel_register_get(set, 0),
						c->lowlevel_register_get(set, 1),
						c->lowlevel_register_get(set, 2),
						c->lowlevel_register_get(set, 3),
						c->lowlevel_register_get(set, 4),
						c->lowlevel_register_get(set, 5)));
	}

	cnsl->put_string_lf(format("PSW: %06o, PC: %06o, run mode: %d", c->getPSW(), c->lowlevel_register_get(0, 7), c->getPSW_runmode()));

	cnsl->put_string_lf(format("STACK: k:%06o, sv:%06o, -:%06o, usr: %06o",
				c->lowlevel_register_sp_get(0),
				c->lowlevel_register_sp_get(1),
				c->lowlevel_register_sp_get(2),
				c->lowlevel_register_sp_get(3)));
}

void show_run_statistics(console *const cnsl, cpu *const c)
{
	auto stats = c->get_mips_rel_speed({ }, { });

	cnsl->put_string_lf(format("Executed %zu instructions in %.2f ms of which %.2f ms idle", size_t(std::get<2>(stats)), std::get<3>(stats) / 1000., std::get<4>(stats) / 1000.));
	cnsl->put_string_lf(format("MIPS: %.2f, relative speed: %.2f%%", std::get<0>(stats), std::get<1>(stats)));
}

void show_queued_interrupts(console *const cnsl, cpu *const c)
{
	cnsl->put_string_lf(format("Current level: %d", c->getPSW_spl()));

	auto delay = c->get_interrupt_delay_left();
	if (delay.has_value())
		cnsl->put_string_lf(format("Current delay left: %d", delay.value()));
	else
		cnsl->put_string_lf("No delay");

	cnsl->put_string_lf(format("Interrupt pending flag: %d", c->check_if_interrupts_pending()));

	auto queued_interrupts = c->get_queued_interrupts();

	for(auto & level: queued_interrupts) {
		for(auto & qi: level.second)
			cnsl->put_string_lf(format("Level: %d, interrupt: %03o", level.first, qi));
	}
}

#if IS_POSIX
void serialize_state(console *const cnsl, const bus *const b, const std::string & filename)
{
	json_t *j = b->serialize();

	bool ok = false;

	FILE *fh = fopen(filename.c_str(), "w");
	if (fh) {
		if (json_dumpf(j, fh, JSON_INDENT(4)) == 0)
			ok = true;

		fclose(fh);
	}

	json_decref(j);

	cnsl->put_string_lf(format("Serialize to %s: %s", filename.c_str(), ok ? "OK" : "failed"));
}
#endif

void debugger(console *const cnsl, bus *const b, std::atomic_uint32_t *const stop_event, const bool tracing_in)
{
	int32_t trace_start_addr = -1;
	bool    tracing          = tracing_in;
	int     n_single_step    = 1;
	bool    turbo            = false;
	std::optional<int> t_rl;  // trace runlevel

	cpu *const c = b->getCpu();

	b->set_debug_mode();

	bool single_step = false;

	while(*stop_event != EVENT_TERMINATE) {
		try {
			std::string cmd   = cnsl->read_line(format("%d", stop_event->load()));
			auto        parts = split(cmd, " ");
			auto        kv    = split(parts, "=");

			if (parts.empty())
				continue;

			if (cmd == "go") {
				single_step = false;

				*stop_event = EVENT_NONE;
			}
			else if (parts[0] == "single" || parts[0] == "s") {
				single_step = true;

				if (parts.size() == 2)
					n_single_step = atoi(parts[1].c_str());
				else
					n_single_step = 1;

				*stop_event = EVENT_NONE;
			}
			else if ((parts[0] == "sbp" || parts[0] == "cbp") && parts.size() >= 2){
				if (parts[0] == "sbp") {
					std::size_t space = cmd.find(" ");

					std::pair<breakpoint *, std::optional<std::string> > rc = parse_breakpoint(b, cmd.substr(space + 1));

					if (rc.first == nullptr) {
						if (rc.second.has_value())
							cnsl->put_string_lf(rc.second.value());
						else
							cnsl->put_string_lf("not set");
					}
					else {
						int id = c->set_breakpoint(rc.first);

						cnsl->put_string_lf(format("Breakpoint has id: %d", id));
					}
				}
				else {
					if (c->remove_breakpoint(std::stoi(parts[1])))
						cnsl->put_string_lf("Breakpoint cleared");
					else
						cnsl->put_string_lf("Breakpoint not found");
				}

				continue;
			}
			else if (cmd == "lbp") {
				auto bps = c->list_breakpoints();

				cnsl->put_string_lf("Breakpoints:");

				for(auto & a : bps)
					cnsl->put_string_lf(format("%d: %s", a.first, a.second->emit().c_str()));

				if (bps.empty())
					cnsl->put_string_lf("(none)");

				continue;
			}
			else if (parts[0] == "disassemble" || parts[0] == "d") {
				uint16_t pc = kv.find("pc") != kv.end() ? std::stoi(kv.find("pc")->second, nullptr, 8)  : c->getPC();
				int n  = kv.find("n")  != kv.end() ? std::stoi(kv.find("n") ->second, nullptr, 10) : 1;

				cnsl->put_string_lf(format("Disassemble %d instructions starting at %o", n, pc));

				bool show_registers = kv.find("pc") == kv.end();

				for(int i=0; i<n; i++) {
					pc += disassemble(c, cnsl, pc, !show_registers);

					show_registers = false;
				}

				continue;
			}
			else if (parts[0] == "setpc") {
				if (parts.size() == 2) {
					uint16_t new_pc = std::stoi(parts.at(1), nullptr, 8);
					c->setPC(new_pc);

					cnsl->put_string_lf(format("Set PC to %06o", new_pc));
				}
				else {
					cnsl->put_string_lf("setpc requires an (octal address as) parameter");
				}

				continue;
			}
			else if (parts[0] == "toggle") {
				auto s_it = kv.find("s");
				auto t_it = kv.find("t");

				if (s_it == kv.end() || t_it == kv.end())
					cnsl->put_string_lf(format("toggle: parameter missing? current switches states: 0o%06o", c->getBus()->get_console_switches()));
				else {
					int s = std::stoi(s_it->second, nullptr, 8);
					int t = std::stoi(t_it->second, nullptr, 8);

					c->getBus()->set_console_switch(s, t);

					cnsl->put_string_lf(format("Set switch %d to %d", s, t));
				}

				continue;
			}
			else if (parts[0] == "setmem") {
				auto a_it = kv.find("a");
				auto v_it = kv.find("v");

				if (a_it == kv.end() || v_it == kv.end())
					cnsl->put_string_lf("setmem: parameter missing?");
				else {
					uint16_t a = std::stoi(a_it->second, nullptr, 8);
					uint8_t  v = std::stoi(v_it->second, nullptr, 8);

					c->getBus()->write_byte(a, v);

					cnsl->put_string_lf(format("Set %06o to %03o", a, v));
				}

				continue;
			}
			else if (parts[0] == "trace" || parts[0] == "t") {
				tracing = !tracing;

				cnsl->put_string_lf(format("Tracing set to %s", tracing ? "ON" : "OFF"));

				continue;
			}
			else if (parts[0] == "mmudump") {
				mmu_dump(cnsl, b, parts.size() == 2 && parts[1] == "-v");

				continue;
			}
			else if (parts[0] == "mmures") {
				if (parts.size() == 2)
					mmu_resolve(cnsl, b, std::stoi(parts[1], nullptr, 8));
				else
					cnsl->put_string_lf("Parameter missing");

				continue;
			}
			else if (parts[0] == "regdump") {
				reg_dump(cnsl, c);

				continue;
			}
			else if (parts[0] == "strace") {
				if (parts.size() != 2) {
					trace_start_addr = -1;

					cnsl->put_string_lf("Tracing start address reset");
				}
				else {
					trace_start_addr = std::stoi(parts[1], nullptr, 8);

					cnsl->put_string_lf(format("Tracing start address set to %06o", trace_start_addr));
				}

				continue;
			}
			else if (parts[0] == "examine" || parts[0] == "e") {
				if (parts.size() < 3)
					cnsl->put_string_lf("parameter missing");
				else {
					uint32_t addr = std::stoi(parts[1], nullptr, 8);
					int      n    = parts.size() == 4 ? atoi(parts[3].c_str()) : 1;

					if (parts[2] != "p" && parts[2] != "v") {
						cnsl->put_string_lf("expected p (physical address) or v (virtual address)");

						continue;
					}

					std::string out;

					for(int i=0; i<n; i++) {
						uint32_t cur_addr = addr + i * 2;
						int val = parts[2] == "v" ? b->read(cur_addr, wm_word, rm_cur, true) : b->readPhysical(cur_addr);

						if (val == -1) {
							cnsl->put_string_lf(format("Can't read from %06o\n", cur_addr));
							break;
						}

						if (n == 1)
							cnsl->put_string_lf(format("value at %06o, octal: %o, hex: %x, dec: %d\n", cur_addr, val, val, val));

						if (n > 1) {
							if (i > 0)
								out += " ";

							out += format("%06o=%06o", cur_addr, val);
						}
					}

					if (n > 1)
						cnsl->put_string_lf(out);
				}

				continue;
			}
			else if (cmd == "reset" || cmd == "r") {
				*stop_event = EVENT_NONE;
				b->reset();
				continue;
			}
			else if (cmd == "cfgdisk") {
				configure_disk(b, cnsl);

				continue;
			}
#if defined(ESP32)
			else if (cmd == "debug") {
				if (heap_caps_check_integrity_all(true) == false)
					cnsl->put_string_lf("HEAP corruption!");

				continue;
			}
			else if (cmd == "cfgnet") {
				configure_network(cnsl);

				continue;
			}
			else if (cmd == "chknet") {
				check_network(cnsl);

				continue;
			}
			else if (cmd == "startnet") {
				start_network(cnsl);

				continue;
			}
			else if (parts.at(0) == "serspd") {
				if (parts.size() == 2) {
					uint32_t speed = std::stoi(parts.at(1), nullptr, 10);
					set_tty_serial_speed(cnsl, speed);

					cnsl->put_string_lf(format("Set baudrate to %d", speed));
				}
				else {
					cnsl->put_string_lf("serspd requires an (decimal) parameter");
				}

				continue;
			}
#endif
			else if (cmd == "stats") {
				show_run_statistics(cnsl, c);

				continue;
			}
			else if (parts[0] == "ramsize") {
				if (parts.size() == 2)
					b->set_memory_size(std::stoi(parts.at(1)));
				else {
					int n_pages = b->getRAM()->get_memory_size();

					cnsl->put_string_lf(format("Memory size: %u pages or %u kB (decimal)", n_pages, n_pages * 8192 / 1024));
				}

				continue;
			}
			else if (parts[0] == "bl" && parts.size() == 2) {
				set_boot_loader(b, parts.at(1) == "rk05" ? BL_RK05 : BL_RL02);
				cnsl->put_string_lf("Bootloader set");

				continue;
			}
			else if (parts[0] == "trl") {
				if (parts.size() == 1)
					t_rl.reset();
				else
					t_rl = std::stoi(parts.at(1));

				continue;
			}
			else if (cmd == "cls") {
				const char cls[] = { 27, '[', '2', 'J', 12, 0 };

				cnsl->put_string_lf(cls);

				continue;
			}
			else if (cmd == "turbo") {
				turbo = !turbo;

				if (turbo)
					c->set_debug(false);

				cnsl->put_string_lf(format("Turbo set to %s", turbo ? "ON" : "OFF"));

				continue;
			}
			else if (cmd == "debug") {
				bool new_mode = !c->get_debug();
				c->set_debug(new_mode);

				cnsl->put_string_lf(format("Debug mode set to %s", new_mode ? "ON" : "OFF"));

				continue;
			}
			else if (parts[0] == "setll" && parts.size() == 2) {
				auto ll_parts = split(parts[1], ",");

				if (ll_parts.size() != 2)
					cnsl->put_string_lf("Loglevel for either screen or file missing");
				else {
					log_level_t ll_screen  = parse_ll(ll_parts[0]);
					log_level_t ll_file    = parse_ll(ll_parts[1]);

					setll(ll_screen, ll_file);
				}
			}
			else if (parts[0] == "setll" && parts.size() == 2) {
				auto ll_parts = split(parts[1], ",");

				if (ll_parts.size() != 2)
					cnsl->put_string_lf("Loglevel for either screen or file missing");
				else {
					log_level_t ll_screen  = parse_ll(ll_parts[0]);
					log_level_t ll_file    = parse_ll(ll_parts[1]);

					setll(ll_screen, ll_file);
				}

				continue;
			}
#if IS_POSIX
			else if (parts[0] == "ser" && parts.size() == 2) {
				serialize_state(cnsl, b, parts.at(1));
				continue;
			}
#endif
			else if (parts[0] == "setsl" && parts.size() == 3) {
				if (setloghost(parts.at(1).c_str(), parse_ll(parts[2])) == false)
					cnsl->put_string_lf("Failed parsing IP address");
				else
					send_syslog(info, "Hello, world!");

				continue;
			}
			else if (cmd == "qi") {
				show_queued_interrupts(cnsl, c);

				continue;
			}
			else if (cmd == "dp") {
				cnsl->stop_panel_thread();

				continue;
			}
			else if (cmd == "bt") {
				if (c->get_debug() == false)
					cnsl->put_string_lf("Debug mode is disabled!");

				auto backtrace = c->get_stack_trace();

				for(auto & element: backtrace)
					cnsl->put_string_lf(format("%06o %s", element.first, element.second.c_str()));

				continue;
			}
			else if (cmd == "quit" || cmd == "q") {
#if defined(ESP32)
				ESP.restart();
#endif
				break;
			}
			else if (cmd == "help" || cmd == "h" || cmd == "?") {
				constexpr const char *const help[] = {
					"disassemble/d - show current instruction (pc=/n=)",
					"go            - run until trap or ^e",
#if !defined(ESP32)
					"quit/q        - stop emulator",
#endif
					"examine/e     - show memory address (<octal address> <p|v> [<n>])",
					"reset/r       - reset cpu/bus/etc",
					"single/s      - run 1 instruction (implicit 'disassemble' command)",
					"sbp/cbp/lbp   - set/clear/list breakpoint(s)",
					"                e.g.: (pc=0123 and memwv[04000]=0200,0300 and (r4=07,05 or r5=0456))",
					"                values seperated by ',', char after mem is w/b (word/byte), then",
					"                follows v/p (virtual/physical), all octal values, mmr0-3 and psw are",
					"                registers",
					"trace/t       - toggle tracing",
					"setll         - set loglevel: terminal,file",
					"setsl         - set syslog target: requires a hostname and a loglevel",
					"turbo         - toggle turbo mode (cannot be interrupted)",
					"debug         - enable CPU debug mode",
					"bt            - show backtrace - need to enable debug first",
					"strace        - start tracing from address - invoke without address to disable",
					"trl           - set trace run-level, empty for all",
					"regdump       - dump register contents",
					"mmudump       - dump MMU settings (PARs/PDRs)",
					"mmures        - resolve a virtual address",
					"qi            - show queued interrupts",
					"setpc         - set PC to value",
					"setmem        - set memory (a=) to value (v=), both in octal, one byte",
					"toggle        - set switch (s=, 0...15 (decimal)) of the front panel to state (t=, 0 or 1)",
					"cls           - clear screen",
					"stats         - show run statistics",
					"ramsize       - set ram size (page count (8 kB))",
					"bl            - set bootload (rl02 or rk05)",
#if IS_POSIX
					"ser           - serialize state to a file",
//					"dser          - deserialize state from a file",
#endif
					"dp            - disable panel",
#if defined(ESP32)
					"cfgnet        - configure network (e.g. WiFi)",
					"startnet      - start network",
					"chknet        - check network status",
					"serspd        - set serial speed in bps (8N1 are default)",
					"debug         - debugging info",
#endif
					"cfgdisk       - configure disk",
					nullptr
				};

				size_t i=0;
				while(help[i])
					cnsl->put_string_lf(help[i++]);
				continue;
			}
			else {
				cnsl->put_string_lf("?");
				continue;
			}

			c->emulation_start();

			*cnsl->get_running_flag() = true;

			bool reset_cpu = true;

			if (turbo) {
				while(*stop_event == EVENT_NONE)
					c->step();
			}
			else {
				reset_cpu = false;

				while(*stop_event == EVENT_NONE) {
					if (!single_step)
						DOLOG(debug, false, "---");

					if (trace_start_addr != -1 && c->getPC() == trace_start_addr)
						tracing = true;

					if ((tracing || single_step) && (t_rl.has_value() == false || t_rl.value() == c->getPSW_runmode()))
						disassemble(c, single_step ? cnsl : nullptr, c->getPC(), false);

					auto bp_result = c->check_breakpoint();
					if (bp_result.has_value() && !single_step) {
						cnsl->put_string_lf("Breakpoint: " + bp_result.value());
						break;
					}

					c->step();

					if (single_step && --n_single_step == 0)
						break;
				}
			}

			*cnsl->get_running_flag() = false;

			if (reset_cpu)
				c->reset();
		}
		catch(const std::exception & e) {
			cnsl->put_string_lf(format("Exception caught: %s", e.what()));
		}
		catch(const int ei) {
			cnsl->put_string_lf(format("Problem: %d", ei));
		}
		catch(...) {
			cnsl->put_string_lf("Unspecified exception caught");
		}
	}
}

void run_bic(console *const cnsl, bus *const b, std::atomic_uint32_t *const stop_event, const bool tracing, const uint16_t start_addr)
{
	cpu *const c = b->getCpu();

	c->setRegister(7, start_addr);

	*cnsl->get_running_flag() = true;

	while(*stop_event == EVENT_NONE) {
		if (tracing)
			disassemble(c, nullptr, c->getPC(), false);

		c->step();
	}

	*cnsl->get_running_flag() = false;
}
