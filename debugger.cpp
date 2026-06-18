// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <fstream>
#include <optional>
#include <unordered_map>
#if IS_POSIX || defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#elif defined(TEENSY4_1)
#else
#include <Arduino.h>
#include <LittleFS.h>
#endif

#include "benchmark.h"
#include "blinkenlights.h"
#include "breakpoint_parser.h"
#include "bus.h"
#if IS_POSIX
#include "comm_posix_tty.h"
#endif
#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
#include "comm_tcp_socket_client.h"
#include "comm_tcp_socket_server.h"
#endif
#include "comm_pst.h"
#include "console.h"
#include "cpu.h"
#if defined(ESP32) 
#include "comm_esp32_hardwareserial.h"
#include "comm_esp32_SC16IS752.h"
#endif
#include "ddp.h"
#include "deqna.h"
#include "disk_backend.h"
#if IS_POSIX || defined(_WIN32)
#include "disk_backend_file.h"
#else
#include "disk_backend_esp32.h"
#endif
#include "disk_backend_nbd.h"
#if defined(linux)
#include "eth_transport_linux.h"
#elif defined(ESP32)
#include "eth_transport_esp32.h"
#elif defined(TEENSY4_1)
#include "eth_transport_teensy4_1.h"
#endif
#include "eth_transport_vxlan.h"
#include "kw11-l.h"
#include "loaders.h"
#include "log.h"
#include "memory.h"
#include "tty.h"
#include "utils.h"


extern blinkenlights *bl;
extern ddp           *ddp_;
extern aint           term_cols;
extern aint           term_lines;

#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
bool network_configured = false;
#if defined(ESP32)
#include "esp32.h"
extern comm_esp32_SC16IS752 *SC16IS752_com_a[2];
extern comm_esp32_SC16IS752 *SC16IS752_com_b[2];
#endif

#include "console_esp32.h"

bool init_sd();

void configure_network(console *const cnsl, const std::optional<std::string> & pars);
void check_network(console *const cnsl);
void start_network(console *const cnsl);
#else
constexpr const bool network_configured = true;
#endif

#if !defined(linux) && !defined(_WIN32) && !defined(__APPLE__) && !defined(__FreeBSD__)
extern SdFs SDinstance;
#endif

#define DZ11_CFG_FILE "dz11.json"

FLASHMEM std::optional<disk_backend *> select_nbd_server(console *const cnsl)
{
	cnsl->flush_input();

	std::string hostname = cnsl->read_line("Enter hostname (or empty to abort): ");
	if (hostname.empty())
		return { };

	std::string port_str = cnsl->read_line("Enter port number (or empty to abort): ");
	if (port_str.empty())
		return { };

	disk_backend *d = new disk_backend_nbd(hostname, std::stoi(port_str));
	if (d->begin(false) == false) {
		cnsl->put_string_lf("Cannot initialize NBD client");
		delete d;
		return { };
	}

	return d;
}

FLASHMEM void start_disk(console *const)
{
#if IS_POSIX || defined(_WIN32)
	return;
#else
	static bool disk_started = false;
	if (disk_started)
		return;

	init_sd();
#endif
}

FLASHMEM void ls_l(console *const cnsl)
{
	start_disk(cnsl);

#if IS_POSIX || defined(_WIN32)
	cnsl->put_string_lf("Files in current directory: ");
#else
	cnsl->put_string_lf("Files on SD-card:");
#endif

#if defined(linux) || defined(__FreeBSD__) || defined(__APPLE__)
	DIR *dir = opendir(".");
	if (!dir) {
		cnsl->put_string_lf("Cannot access directory");
		return;
	}

	dirent *dr = nullptr;
	while((dr = readdir(dir))) {
		struct stat st { };

		if (stat(dr->d_name, &st) == 0)
			cnsl->put_string_lf(format("%s\t\t%ld", dr->d_name, st.st_size));
	}

	closedir(dir);
#elif defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
	auto root = SD.open("/");

	for(;;) {
		auto entry = root.openNextFile();
		if (!entry)
			break;

		if (!entry.isDirectory()) {
#if defined(TEENSY4_1) || defined(BUILD_FOR_PICO2W)
			cnsl->put_string_lf(format("%s\t\t%d", entry.name(), int(entry.size())));
#else
			char buffer[32] { };
			entry.getName(buffer, sizeof buffer);
			cnsl->put_string_lf(format("%s\t\t%ld", buffer, entry.size()));
#endif
		}

		entry.close();
	}
#elif defined(_WIN32)
#else
	SDinstance.ls("/", LS_DATE | LS_SIZE | LS_R);
#endif
}

FLASHMEM std::optional<std::string> select_host_file(console *const cnsl)
{
	for(;;) {
		cnsl->flush_input();

		std::string selected_file = cnsl->read_line("Enter filename (\"dir\" for listing or empty to abort): ");

		if (selected_file.empty())
			return { };

		if (selected_file != "dir") {
			cnsl->put_string("Opening file: ");
			cnsl->put_string_lf(selected_file.c_str());

			bool can_open_file = false;

#if IS_POSIX || defined(_WIN32)
			struct stat st { };
			can_open_file = ::stat(selected_file.c_str(), &st) == 0;
#else
			File32 fh;
			can_open_file = fh.open(selected_file.c_str(), O_RDWR);
			if (can_open_file)
				fh.close();
#endif

			if (can_open_file)
				return selected_file;

			cnsl->put_string_lf("open failed");
		}

		ls_l(cnsl);
	}
}

// disk image files
FLASHMEM std::optional<disk_backend *> select_disk_file(console *const cnsl)
{
	start_disk(cnsl);

	for(;;) {
		auto selected_file = select_host_file(cnsl);

		if (selected_file.has_value() == false)
			break;

#if IS_POSIX || defined(_WIN32)
		disk_backend *temp = new disk_backend_file(selected_file.value());
#else
		disk_backend *temp = new disk_backend_esp32(selected_file.value());
#endif

		if (!temp->begin(false)) {
			cnsl->put_string("Cannot use: ");
			cnsl->put_string_lf(selected_file.value().c_str());

			delete temp;

			continue;
		}

		return { temp };
	}

	return { };
}

FLASHMEM int wait_for_key(const std::string & title, console *const cnsl, const std::vector<char> & allowed)
{
	cnsl->put_string(title);
	cnsl->put_string(" > ");

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

FLASHMEM void configure_comm(console *const cnsl, comm_io *const device_list)
{
	for(;;) {
		std::vector<char> keys_allowed { '9' };
		int               slot_key     { 'A' };
		for(size_t i=0; i<device_list->channels.size(); i++) {
			cnsl->put_string_lf(format(" %c. %s", slot_key, device_list->get_identifier(i).c_str()));
			keys_allowed.push_back(slot_key);
			slot_key++;
		}

		int ch_dev = wait_for_key("Select communication device to setup or 9. to exit", cnsl, keys_allowed);
		if (ch_dev == '9')
			break;

		size_t device_nr = ch_dev - 'A';

		int  ch_opt = wait_for_key("1. TCP client, 2. TCP server, 3. serial device, 4. SC16IS752, 5. PST emulation, 9. to abort", cnsl, { '1', '2', '3', '4', '5', '9' });
		bool rc     = false;

		if (false) {
		}
#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
		else if (ch_opt == '1') {
			std::string temp_host = cnsl->read_line("host: ");
			std::string temp_port = temp_host.empty() ? "" : cnsl->read_line("port: ");

			if (temp_host.empty() == false && temp_port.empty() == false)
				rc = device_list->set_device(device_nr, new comm_tcp_socket_client(temp_host, std::stoi(temp_port)));
		}
		else if (ch_opt == '2') {
			std::string temp = cnsl->read_line("port: ");
			if (temp.empty() == false) {
				int ch_dev = wait_for_key("Initialize telnet session? (y/n)", cnsl, { 'y', 'n' });
				rc = device_list->set_device(device_nr, new comm_tcp_socket_server(std::stoi(temp), ch_dev == 'y'));
			}
		}
#endif
		else if (ch_opt == '3') {
#if IS_POSIX
			std::string temp_dev     = cnsl->read_line("device: ");
			std::string temp_bitrate = cnsl->read_line("bitrate: ");
			if (temp_dev.empty() == false && temp_bitrate.empty() == false)
				rc = device_list->set_device(device_nr, new comm_posix_tty(temp_dev, std::stoi(temp_bitrate)));
#elif defined(ESP32)
			std::string temp_dev = cnsl->read_line("Uart number (0...2): ");
			std::string temp_rx  = cnsl->read_line("RX pin: ");
			std::string temp_tx  = cnsl->read_line("TX pin: ");
			std::string temp_bitrate = cnsl->read_line("bitrate: ");
			if (temp_dev.empty() == false && temp_bitrate.empty() == false && temp_rx.empty() == false && temp_tx.empty() == false)
				rc = device_list->set_device(device_nr, new comm_esp32_hardwareserial(uart_port_t(std::stoi(temp_dev)), std::stoi(temp_rx), std::stoi(temp_tx), std::stoi(temp_bitrate)));
#else
			cnsl->put_string_lf("Not implemented yet on this platform");
#endif
		}
		else if (ch_opt == '4') {
#if defined(ESP32)
			std::string temp_port    = cnsl->read_line("port (A/B): ");
			std::string temp_bitrate = cnsl->read_line("bitrate: ");
			if (temp_port.empty() == false && temp_bitrate.empty() == false) {
				int port = toupper(temp_port[0]) - 'A';
				if (port < 0 || port > 1)
					continue;

				// currently only 1 SC16IS752
				auto new_dev = SC16IS752_com_a[port];
				new_dev->configure_port(std::stoi(temp_bitrate));
				rc = device_list->set_device(device_nr, new_dev);
			}
#else
			cnsl->put_string_lf("Only on microcontrollers");
#endif
		}
		else if (ch_opt == '5') {
#if WITH_PPS
			std::string pps_dev = cnsl->read_line("PPS device name: ");
			if (pps_dev.empty() == false)
				rc = device_list->set_device(device_nr, new comm_pst(pps_dev));
#elif !defined(BUILD_FOR_PICO2W)
			rc = device_list->set_device(device_nr, new comm_pst("-"));
#endif
		}

		if (ch_opt != 9 && rc == false)
			cnsl->put_string_lf("Failed to initialize device");
	}
}

FLASHMEM std::optional<disk_backend *> select_disk_backend(console *const cnsl)
{
	int ch = wait_for_key("1. local disk, 2. network disk (NBD), 9. abort", cnsl, { '1', '2', '9' });
	if (ch == '9')
		return { };

	if (ch == '1')
		return select_disk_file(cnsl);

	if (ch == '2') {
		if (network_configured)
			return select_nbd_server(cnsl);
		cnsl->put_string_lf("Please configure network first (cfgnet)");
	}

	return { };
}

FLASHMEM void add_new_rk05(bus *const b, console *const cnsl)
{
	if (b->getRK05() == nullptr) {
		auto rk05_dev = new rk05(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
		rk05_dev->begin();
		b->add_rk05(rk05_dev);
	}
}

FLASHMEM void add_new_rl02(bus *const b, console *const cnsl)
{
	if (b->getRL02() == nullptr) {
		auto rl02_dev = new rl02(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
		rl02_dev->begin();
		b->add_rl02(rl02_dev);
	}
}

FLASHMEM void add_new_rp06(bus *const b, console *const cnsl)
{
	if (b->getRP06() == nullptr) {
		auto rp06_dev = new rp06(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag(), false);
		rp06_dev->begin();
		b->add_RP06(rp06_dev);
	}
}

FLASHMEM void configure_disk(bus *const b, console *const cnsl)
{
	int type_ch = wait_for_key("1. RK05, 2. RL02, 3. RP06, 9. abort", cnsl, { '1', '2', '3', '9' });

	bootloader_t bl = BL_NONE;
	disk_device *dd = nullptr;

	if (type_ch == '1') {
		add_new_rk05(b, cnsl);
		dd = b->getRK05();
		bl = BL_RK05;
	}
	else if (type_ch == '2') {
		add_new_rl02(b, cnsl);
		dd = b->getRL02();
		bl = BL_RL02;
	}
	else if (type_ch == '3') {
		add_new_rp06(b, cnsl);
		dd = b->getRP06();
		bl = BL_RP06;
	}
	else if (type_ch == '9') {
		return;
	}

	for(;;) {
		std::vector<char> keys_allowed { '1', '2', '9' };

		auto cartridge_slots = dd->access_disk_backends();
		int  slot_key        = 'A';
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
			auto bl_addr = set_boot_loader(b, bl);
			if (bl_addr.has_value()) {
				cnsl->put_string_lf("Bootloader loaded");
				b->getCpu()->setPC(bl_addr.value());
			}
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

// returns size of instruction (in bytes), duration and if it was accessing something different from RAM/ROM
FLASHMEM std::tuple<int, uint32_t, bool, std::string> disassemble(cpu *const c, console *const cnsl, const uint16_t pc, const bool instruction_only)
{
	auto data      = c->disassemble(pc);
	if (data.empty())
		return { 2, 0, true, "?" };  // problem!

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

	uint32_t duration   = std::stoi(data["duration"].at(0));
	float    duration_f = duration / 1000.;

	std::string result;

	if (instruction_only)
		result = format("PC: %06o, instr: %-20s %-13s %5.2fus %s",
				pc,
				instruction_values.c_str(),
				work_values.c_str(),
				duration_f,
				instruction.c_str());
	else
		result = format("R0: %s, R1: %s, R2: %s, R3: %s, R4: %s, R5: %s, SP: %s, PC: %06o, PSW: %s (%s), instr: %s: %s (%.3f us)",
				registers[0].c_str(), registers[1].c_str(), registers[2].c_str(), registers[3].c_str(), registers[4].c_str(),
				registers[5].c_str(), registers[6].c_str(), pc,
				psw.c_str(), data["psw-value"][0].c_str(),
				instruction_values.c_str(),
				instruction.c_str(), duration_f);

	std::string sp;
	for(auto sp_val : data["sp"])
		sp += (sp.empty() ? "" : ",") + sp_val;

	DOLOG(log_ss::LS_TRACE, "SP: %s, MMR0/1/2/3: %s/%s/%s/%s", sp.c_str(), MMR0.c_str(), MMR1.c_str(), MMR2.c_str(), MMR3.c_str());

	if (cnsl)
		cnsl->put_string_lf(result);

	return { data["instruction-values"].size() * 2, duration, std::stoi(data["works-on-io"][0]), result };
}

FLASHMEM std::map<std::string, std::string> split(const std::vector<std::string> & kv_array, const std::string & splitter)
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

FLASHMEM const char *trap_action_to_str(const trap_action_t ta)
{
	if (ta == T_PROCEED)
		return "proceed";
	if (ta == T_ABORT_4)
		return "abort (trap 4)";
	if (ta == T_TRAP_250)
		return "trap 250";

	return "?";
}

FLASHMEM void mmu_resolve(console *const cnsl, bus *const b, const uint16_t va)
{
	int  run_mode = b->getCpu()->getPSW_runmode();
	cnsl->put_string_lf(format("Run mode: %d, use data space: %d", run_mode, b->getMMU()->get_use_data_space(run_mode)));

	auto data     = b->getMMU()->calculate_physical_address(run_mode, va);

	uint16_t page_offset = va & 8191;
	cnsl->put_string_lf(format("Active page field: %d, page offset: %o (%d)", data.apf, page_offset, page_offset));
	cnsl->put_string_lf(format("Phys. addr. instruction: %08o (psw: %d)", data.physical_instruction, data.physical_instruction_is_psw));
	cnsl->put_string_lf(format("Phys. addr. data: %08o (psw: %d)", data.physical_data, data.physical_data_is_psw));

	uint16_t mmr3 = b->getMMU()->getMMR3();

	if (run_mode == 0) {
		b->getMMU()->dump_par_pdr(cnsl, 1, i_space, "supervisor i-space", 0,                  data.apf);
		b->getMMU()->dump_par_pdr(cnsl, 1, d_space, "supervisor d-space", 1 + (!!(mmr3 & 4)), data.apf);
	}
	else if (run_mode == 1) {
		b->getMMU()->dump_par_pdr(cnsl, 0, i_space, "kernel i-space",     0,                  data.apf);
		b->getMMU()->dump_par_pdr(cnsl, 0, d_space, "kernel d-space",     1 + (!!(mmr3 & 4)), data.apf);
	}
	else if (run_mode == 3) {
		b->getMMU()->dump_par_pdr(cnsl, 3, i_space, "user i-space",       0,                  data.apf);
		b->getMMU()->dump_par_pdr(cnsl, 3, d_space, "user d-space",       1 + (!!(mmr3 & 4)), data.apf);
	}

	for(int i=0; i<2; i++) {
		auto mmu        = b->getMMU();
		int  page_index = mmu->calc_par_pdr_index(run_mode, d_space, data.apf);
		auto ta_i       = mmu->get_trap_action(page_index + 0, i);
		auto ta_d       = mmu->get_trap_action(page_index + 8, i);

		cnsl->put_string_lf(format("Instruction action: %s (%s)", trap_action_to_str(ta_i.first), i ? "write" : "read"));
		cnsl->put_string_lf(format("Data action       : %s (%s)", trap_action_to_str(ta_d.first), i ? "write" : "read"));
	}
}

FLASHMEM void show_cpu_state(console *const cnsl, cpu *const c)
{
	for(int set=0; set<2; set++) {
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

	cnsl->put_string_lf(format("STACK: k:%06o, sv:%06o, -:%06o, usr:%06o",
				c->lowlevel_register_sp_get(0),
				c->lowlevel_register_sp_get(1),
				c->lowlevel_register_sp_get(2),
				c->lowlevel_register_sp_get(3)));

	auto queued_interrupts = c->get_queued_interrupts();
	for(int i=0; i<8; i++) {
		if (queued_interrupts[i].empty() == false) {
			cnsl->put_string(format("interrupt level: %d, queued:", i));
			for(auto & vector: queued_interrupts[i])
				cnsl->put_string(format(" %03o", vector));
			cnsl->put_string_lf("");
		}
	}
	std::unordered_map<uint16_t, uint32_t> trap_counts = c->get_trap_counts();
	for(auto & vector: trap_counts)
		cnsl->put_string_lf(format("vector %06o count: %u", vector.first, vector.second));
	cnsl->put_string_lf(format("stack limit register: %06o", c->get_stack_limit_register()));
}

FLASHMEM void show_queued_interrupts(console *const cnsl, cpu *const c)
{
	cnsl->put_string_lf(format("Current level: %d", c->getPSW_spl()));

	cnsl->put_string_lf(format("Interrupt pending flag: %d", c->check_if_interrupts_pending()));

	auto queued_interrupts = c->get_queued_interrupts();

	for(int level=0; level<8; level++) {
		for(auto & qi: queued_interrupts[level])
			cnsl->put_string_lf(format("Level: %d, interrupt: %03o", level, qi));
	}
}

struct state_writer {
	FILE *fh = nullptr;

	size_t write(uint8_t c) {
		assert(fh);
		return fputc(c, fh) == EOF ? 0 : 1;
	}

	size_t write(const uint8_t *buffer, size_t length) {
		assert(fh);
		return fwrite(buffer, 1, length, fh);
	}
};

#if IS_POSIX
void serialize_state(console *const cnsl, const bus *const b, const std::string & filename)
{
	JsonDocument j = b->serialize();

	bool ok = false;

#if !defined(TEENSY4_1)
	FILE *fh = fopen(filename.c_str(), "w");
	if (fh) {
		state_writer ws { fh };
		serializeJsonPretty(j, ws);
		fclose(fh);

		ok = true;
	}
#endif

	cnsl->put_string_lf(format("Serialize to %s: %s", filename.c_str(), ok ? "OK" : "failed"));
}
#endif

void tm11_load_tape(console *const cnsl, bus *const b, const std::optional<std::string> & file)
{
#if !defined(TEENSY4_1)
	auto *dev = b->getTM11();
	if (dev == nullptr) {
		cnsl->put_string_lf("Adding TM-11");
		dev = new tm_11(b);
		b->add_tm11(dev);
	}

	if (file.has_value())
		dev->load(file.value());
	else {
		auto sel_file = select_host_file(cnsl);

		if (sel_file.has_value())
			dev->load(sel_file.value());
	}
#endif
}

void tm11_unload_tape(bus *const b)
{
#if !defined(TEENSY4_1)
	auto *dev = b->getTM11();
	if (dev)
		dev->unload();
#endif
}

FLASHMEM void set_kw11_l_interrupt_freq(console *const cnsl, bus *const b, const int freq)
{
	if (freq >= 1 && freq < 1000)
		b->getKW11_L()->set_interrupt_frequency(freq);
	else
		cnsl->put_string_lf("Frequency out of range");
}

FLASHMEM device *name_to_dev(bus *const b, const std::string & name)
{
	if (name == "rl02")
		return b->getRL02();
	if (name == "mmu")
		return b->getMMU();
	if (name == "rk05")
		return b->getRK05();
	if (name == "dc11")
		return b->getDC11();
	if (name == "dz11")
		return b->getDZ11();
#if !defined(TEENSY4_1)
	if (name == "tm11")
		return b->getTM11();
#endif
	if (name == "kw11l")
		return b->getKW11_L();
	if (name == "rp06" || name == "rp07")
		return b->getRP06();
	if (name == "deqna")
		return b->getDEQNA();
	return nullptr;
}

FLASHMEM bool trace_enabled()
{
	return (get_log_ss_masks(true) | get_log_ss_masks(false)) & log_ss_type(log_ss::LS_TRACE);
}

struct debugger_state {
	int      n_single_step      {  1    };
	bool     turbo              { false };
	bool     marker             { false };
	bool     single_step        { false };
	bool     pc_monitor_enabled { false };
	bool     go_verbose         { false };
	unsigned pc_monitor_count   { 0 };
	unsigned pc_monitor_counter { 0 };
	std::unordered_map<uint16_t, std::pair<uint32_t, int> > pc_monitor;
};

enum cmd_rc { debugger_continue, debugger_stop, start_emulation };

using cmd_func_t = cmd_rc (*)(console *const, const std::vector<std::string>&, bus *const, cpu *const, debugger_state *const, kek_event_t *const);

struct cmd_pair {
	const char       *const command;
	const char       *const parameters;
	const char       *const descr;
	const cmd_func_t func;
	enum { par_yes, par_no, par_optional } par_t;
};

FLASHMEM cmd_rc cmd_reset(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const c, debugger_state *const, kek_event_t *const stop_event)
{
	*stop_event = EVENT_NONE;
	bool hard = parts.size() == 2 && parts[1] == "hard";
	b->reset(hard);
	b->getRAM()->reset(hard);
	if (hard)
		c->reset();
	cnsl->put_string_lf("resetted");
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_disassemble(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const c, debugger_state *const, kek_event_t *const)
{
	auto     kv = split(parts, "=");
	uint16_t pc = kv.find("pc") != kv.end() ? std::stoi(kv.find("pc")->second, nullptr, 8)  : c->getPC();
	int      n  = kv.find("n")  != kv.end() ? std::stoi(kv.find("n") ->second, nullptr, 10) : 1;

	cnsl->put_string_lf(format("Disassemble %d instructions starting at %o", n, pc));

	bool show_registers = kv.find("pc") == kv.end();

	for(int i=0; i<n; i++) {
		pc += std::get<0>(disassemble(c, cnsl, pc, !show_registers));
		show_registers = false;
	}

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_go(console *const, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const state, kek_event_t *const stop_event)
{
	state->single_step = false;
	state->go_verbose  = parts.size() == 2 && parts[1] == "-v";
	*stop_event        = EVENT_NONE;
	return start_emulation;
}

FLASHMEM cmd_rc cmd_benchmark(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const stop_event)
{
	bool verbose  = false;
	bool with_mmu = false;

	for(size_t i=1; i<parts.size(); i++) {
		verbose  |= parts[i] == "-v";
		with_mmu |= parts[i] == "-m";
	}

	cnsl->put_string_lf("Stopping panel first");
	cnsl->stop_panel_thread();

	cnsl->put_string_lf("Proceeding with enabling KW11-L interrupt");
	*cnsl->get_running_flag() = true;  // enable the KW11-L interrupt
	benchmark(cnsl, b, stop_event, verbose, with_mmu);
	cnsl->put_string_lf("Disabling KW11-L interrupt");
	*cnsl->get_running_flag() = false;

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_quit(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	bool yes = (parts.size() == 2 && parts[1] == "-y") || wait_for_key("y/n", cnsl, { 'y', 'n' }) == 'y';
	if (yes) {
#if defined(ESP32)
		ESP.restart();
#elif defined(BUILD_FOR_PICO2W)
		rp2040.reboot();
#elif defined(TEENSY4_1)
		SRC_GPR5 = 0x0BAD00F1;
		SCB_AIRCR = 0x05FA0004;
#endif
		return debugger_stop;
	}
	return debugger_continue;
}

#if defined(BUILD_FOR_PICO2W)
FLASHMEM cmd_rc cmd_flash(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	int ch_opt = wait_for_key("y/n", cnsl, { 'y', 'n' });
	if (ch_opt == 'y') {
		rp2040.rebootToBootloader();
		return debugger_stop;
	}
	return debugger_continue;
}
#endif

FLASHMEM cmd_rc cmd_examine(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const c, debugger_state *const, kek_event_t *const)
{
	if (parts.size() < 3)
		cnsl->put_string_lf("Parameter(s) missing");
	else {
		uint32_t addr = std::stoi(parts[1], nullptr, 8);
		int      n    = parts.size() == 4 ? std::stoi(parts[3]) : 1;

		if (parts[2] != "p" && parts[2] != "v") {
			cnsl->put_string_lf("expected p (physical address) or v (virtual address)");
			return debugger_continue;
		}

		std::string out;

		for(int i=0; i<n; i++) {
			uint32_t cur_addr = addr + i * 2;
			uint16_t val      = 0;

			if (parts[2] == "v") {
				auto v = b->peek_word(c->getPSW_runmode(), cur_addr);

				if (v.has_value() == false) {
					cnsl->put_string_lf(format("Can't read from %06o\n", cur_addr));
					break;
				}

				val = v.value();
			}
			else {
				val = b->read_physical(cur_addr);
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

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_sbp(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const c, debugger_state *const, kek_event_t *const)
{
	breakpoint::bp_action action     = breakpoint::invalid;
	const std::string   & action_str = parts[1];
	if (action_str == "break" || action_str == "stop")
		action = breakpoint::stop_running;
	else if (action_str == "trace")
		action = breakpoint::start_tracing;
	else if (action_str == "log")
		action = breakpoint::only_log_entry;
	else
		cnsl->put_string_lf(format("\"%s\" is an unknown breakpoint action", action_str.c_str()));

	if (action != breakpoint::invalid) {
		std::string bp_def;
		for(size_t i=2; i<parts.size(); i++) {
			if (i != 2)
				bp_def += " ";
			bp_def += parts[i];
		}

		std::pair<breakpoint *, std::optional<std::string> > rc = parse_breakpoint(b, bp_def, action);

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
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_cbp(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const c, debugger_state *const, kek_event_t *const)
{
	if (c->remove_breakpoint(std::stoi(parts[1])))
		cnsl->put_string_lf("Breakpoint cleared");
	else
		cnsl->put_string_lf("Breakpoint not found");
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_lbp(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const c, debugger_state *const, kek_event_t *const)
{
	cnsl->put_string_lf("Breakpoints:");

	auto bps = c->list_breakpoints();
	for(auto & a : bps)
		cnsl->put_string_lf(format("%d: %s", a.first, a.second->emit().c_str()));
	if (bps.empty())
		cnsl->put_string_lf("(none)");

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_help(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const c, debugger_state *const state, kek_event_t *const stop_event);

FLASHMEM cmd_rc cmd_single(console *const, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const state, kek_event_t *const stop_event)
{
	state->single_step = true;
	if (parts.size() == 2)
		state->n_single_step = std::stoi(parts[1]);
	else
		state->n_single_step = 1;

	*stop_event = EVENT_NONE;
	return start_emulation;
}

FLASHMEM cmd_rc cmd_trace(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	bool is_console = parts[1] == "console" || parts[1] == "con";
	if (parts.size() == 3)
		set_ss_log(is_console, parts[2] == "on" || parts[2] == "ON" ? log_ss::LS_TRACE : log_ss(0));

	cnsl->put_string_lf(format("Tracing set to %s", get_log_ss_masks(is_console) & log_ss_type(log_ss::LS_TRACE) ? "ON" : "OFF"));
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_getlss(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	bool is_console = parts[1] == "console" || parts[1] == "con";
	cnsl->put_string_lf("Enabled subsystems logging: " + get_ss_mask(is_console));
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_list_ss(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	cnsl->put_string_lf("Available subsystems: " + get_all_available_log_ss_masks());
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_clss(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	if (parts.size() == 2) {
		bool is_console = parts[1] == "console" || parts[1] == "con";
		disable_all_log_ss(is_console);
		cnsl->put_string_lf("OK");
	}
	else if (parts.size() == 1) {
		disable_all_log_ss(true );
		disable_all_log_ss(false);
		cnsl->put_string_lf("OK");
	}
	else {
		cnsl->put_string_lf("?");
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_toggle_ss(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	bool is_console = parts[1] == "console" || parts[1] == "con";
	for(size_t i=2; i<parts.size(); i++) {
		if (toggle_ss_log(is_console, parts[i]))
			cnsl->put_string_lf(parts[i] + " toggled");
		else
			cnsl->put_string_lf(parts[i] + " not known");
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_setsl(console *const, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	setloghost(parts[1].c_str());
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_pts(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	cnsl->enable_timestamp(std::stoi(parts[1]));
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_turbo(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const state, kek_event_t *const)
{
	state->turbo = !state->turbo;
	cnsl->put_string_lf(format("Turbo set to %s", state->turbo ? "ON" : "OFF"));
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_state(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const c, debugger_state *const, kek_event_t *const)
{
	if (parts.size() == 1)
		cnsl->put_string_lf("Parameter(s) missing");
	else if (parts[1] == "reset") {
		if (parts.size() < 3)
			cnsl->put_string_lf("Parameter(s) missing");
		else {
			bool        hard = false;
			std::string name;

			if (parts.size() == 4) {
				name = parts[3];
				hard = parts[2] == "hard";
			}
			else {
				name = parts[2];
			}

			if (name == "cpu")
				show_cpu_state(cnsl, c);
			else {
				device *dev = name_to_dev(b, name);
				if (dev == nullptr)
					cnsl->put_string_lf(format("Device \"%s\" is not known", name.c_str()));
				else
					dev->reset(hard);
			}
		}
	}
	else {
		if (parts[1] == "cpu")
			show_cpu_state(cnsl, c);
		else {
			device *dev = name_to_dev(b, parts[1]);
			if (dev == nullptr)
				cnsl->put_string_lf(format("Device \"%s\" is not known", parts[1].c_str()));
			else
				dev->show_state(cnsl);
		}
	}

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_mmures(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	if (parts.size() != 2)
		mmu_resolve(cnsl, b, std::stoi(parts[1], nullptr, 8));
	else
		cnsl->put_string_lf("Parameter count invalid");
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_qi(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const c, debugger_state *const, kek_event_t *const)
{
	show_queued_interrupts(cnsl, c);
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_setreg(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const c, debugger_state *const, kek_event_t *const)
{
	if (parts.size() == 3) {
		int      reg = std::stoi(parts.at(1));
		uint16_t val = std::stoi(parts.at(2), nullptr, 8);
		c->set_register(reg, val);

		cnsl->put_string_lf(format("Set register %d to %06o", reg, val));
	}
	else {
		cnsl->put_string_lf("setreg requires a register and an octal value");
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_setpc(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const c, debugger_state *const, kek_event_t *const)
{
	if (parts.size() == 2) {
		uint16_t new_pc = std::stoi(parts.at(1), nullptr, 8);
		c->setPC(new_pc);
		cnsl->put_string_lf(format("Set PC to %06o", new_pc));
	}
	else {
		cnsl->put_string_lf("setpc requires an (octal address as) parameter");
	}

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_setstack(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const c, debugger_state *const, kek_event_t *const)
{
	if (parts.size() == 3) {
		int      reg = std::stoi(parts.at(1));
		uint16_t val = std::stoi(parts.at(2), nullptr, 8);
		if (reg < 4) {
			c->set_stackpointer(reg, val);
			cnsl->put_string_lf(format("Set stack register %d to %06o", reg, val));
		}
	}
	else {
		cnsl->put_string_lf("setstack requires a register and an octal value");
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_setpsw(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const c, debugger_state *const, kek_event_t *const)
{
	if (parts.size() == 2) {
		uint16_t val = std::stoi(parts.at(1), nullptr, 8);
		c->lowlevel_psw_set(val);

		cnsl->put_string_lf(format("Set PSW to %06o", val));
	}
	else {
		cnsl->put_string_lf("setpsw requires an octal value");
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_deposit(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const c, debugger_state *const, kek_event_t *const)
{
	if (parts.size() != 3)
		cnsl->put_string_lf("Parameter count invalid");
	else {
		uint16_t v = std::stoi(parts[2], nullptr, 8);
		if (parts[1] == "pc" || parts[1] == "PC") {
			c->setPC(v);
			cnsl->put_string_lf(format("Set PC to %06o", v));
		}
		else {
			uint16_t a = std::stoi(parts[1], nullptr, 8);
			b->write_word(a, v);
			cnsl->put_string_lf(format("Set %06o to %06o", a, v));
		}
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_setmem(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	auto kv   = split(parts, "=");
	auto a_it = kv.find("a");
	auto v_it = kv.find("v");

	if (a_it == kv.end() || v_it == kv.end())
		cnsl->put_string_lf("Parameter(s) missing");
	else {
		uint16_t a = std::stoi(a_it->second, nullptr, 8);
		uint8_t  v = std::stoi(v_it->second, nullptr, 8);

		b->write_byte(a, v);

		cnsl->put_string_lf(format("Set %06o to %03o", a, v));
	}

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_getmem(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	auto kv   = split(parts, "=");
	auto a_it = kv.find("a");

	if (a_it == kv.end())
		cnsl->put_string_lf("Parameter(s) missing");
	else {
		uint16_t a = std::stoi(a_it->second, nullptr, 8);
		cnsl->put_string_lf(format("MEM %06o = %03o", a, b->read_byte(a)));
	}

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_toggle(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	auto kv   = split(parts, "=");
	auto s_it = kv.find("s");
	auto t_it = kv.find("t");

	if (s_it == kv.end() || t_it == kv.end())
		cnsl->put_string_lf(format("toggle: parameter missing? current switches states: 0o%06o", b->get_console_switches()));
	else {
		int s = std::stoi(s_it->second, nullptr, 8);
		int t = std::stoi(t_it->second, nullptr, 8);

		b->set_console_switch(s, t);

		cnsl->put_string_lf(format("Set switch %d to %d", s, t));
	}

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_pcmon(console *const, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const state, kek_event_t *const)
{
	state->pc_monitor_enabled = true;
	state->pc_monitor_count   = std::stoi(parts.at(1));
	state->pc_monitor_counter = 0;
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_deqna(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	b->add_DEQNA(nullptr);  // disable & remove any existing

	uint8_t mac[6] { };
	get_deqna_mac(mac);
	cnsl->put_string_lf(format("MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]));

	eth_transport *dev  = nullptr;
	auto           pars = split(parts[1], ",");
	if (false) {
	}
#if defined(linux)
	else if (pars[0] == "linux") {
		if (pars.size() != 2)
			cnsl->put_string_lf("Invalid parameter count");
		else
			dev = new eth_transport_linux(pars[1]);
	}
#elif defined(ESP32)
	else if (pars[0] == "esp32") {
		dev = new eth_transport_esp32(mac);
	}
#elif defined(TEENSY4_1)
	else if (pars[0] == "teensy4.1") {
		dev = new eth_transport_teensy4_1(mac);
	}
#endif
	else if (pars[0] == "vxlan") {
		if (pars.size() == 3)
			dev = new eth_transport_vxlan(pars[1], std::stoi(pars[2]));
		else if (pars.size() == 4)
			dev = new eth_transport_vxlan(pars[1], std::stoi(pars[2]), std::stoi(pars[3]));
		else
			cnsl->put_string_lf("Invalid parameter count");
	}
	else {
		cnsl->put_string_lf(format("\"%s\" is not understood or parameters missing", pars[0].c_str()));
	}

	if (dev) {
		if (false) {
		}
#if !defined(WAVESHARE_S3_ETH) && !defined(TEENSY4_1)
		else if (!network_configured)
			cnsl->put_string_lf("Please configure network first (cfgnet)");
#endif
		else if (dev->begin()) {
			auto d = new deqna(b, mac, dev, cnsl->get_network_activity_flag());
			if (d->begin()) {
				cnsl->put_string_lf("DEQNA emulation initialized");
				b->add_DEQNA(d);
			}
			else {
				cnsl->put_string_lf("DEQNA emulation initialization failed");
				delete d;
				delete dev;
			}
		}
		else {
			delete dev;
			cnsl->put_string_lf("DEQNA emulation initialization failed");
		}
	}

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_test(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const stop_event)
{
	std::string which;
	bool        repeat = false;
	int         delay  = 0;

	for(size_t i=1; i<parts.size(); i++) {
		if (parts[i] == "-r")
			repeat = true;
		else if (parts[i] == "-d")
			delay = std::stoi(parts[++i]);
		else
			which = parts[i];
	}

	do {
		if (which == "deqna") {
			auto deqna = b->getDEQNA();
			if (deqna) {
				if (deqna->test(cnsl))
					cnsl->put_string_lf("DEQNA test succeeded!");
				else
					cnsl->put_string_lf("DEQNA test failed");
			}
			else {
				cnsl->put_string_lf("DEQNA emulation is not configured yet");
			}
		}
		else if (which =="dz11") {
			if (b->getDZ11())
				b->getDZ11()->test_ports(parts.size() == 3 ? std::stoi(parts[2]) : 1);
			else
				cnsl->put_string_lf("DZ11 not started yet, first invoke \"startnet\"");
		}
		else if (which =="panel") {
			cnsl->test_panel();
		}
		else {
			cnsl->put_string_lf("?");
		}

		if (delay)
			myusleep(delay * 1000);
	}
	while(repeat && load_relaxed_p(stop_event) == EVENT_NONE);

	return debugger_continue;
}

FLASHMEM cmd_rc cmd_mdeqna(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	auto deqna = b->getDEQNA();
	if (deqna) {
		deqna::monitor_mode_t mode = deqna::nothing;

		if (parts[1] == "filtered")
			mode = deqna::filtered;
		else if (parts[1] == "everything")
			mode = deqna::everything;
		else if (parts[1] == "ll-trace")
			mode = deqna::ll_trace;
		else if (parts[1] == "none") {
		}
		else
			cnsl->put_string_lf("?");

		deqna->set_monitor_mode(mode, cnsl);
	}
	else {
		cnsl->put_string_lf("DEQNA emulation is not configured yet");
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_cfgdisk(console *const cnsl, const std::vector<std::string> &, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	configure_disk(b, cnsl);
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_cdz11(console *const cnsl, const std::vector<std::string> &, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	if (b->getDZ11())
		configure_comm(cnsl, b->getDZ11()->get_comm_interfaces());
	else
		cnsl->put_string_lf("DZ11 not started yet, first invoke \"startnet\"");
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_setinthz(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	set_kw11_l_interrupt_freq(cnsl, b, std::stoi(parts.at(1)));
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_getinthz(console *const cnsl, const std::vector<std::string> &, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	cnsl->put_string_lf(format("kw11 Hz: %d", b->getKW11_L()->get_interrupt_frequency()));
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_blights(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	if (network_configured == false)
		cnsl->put_string_lf("Please configure network first (cfgnet)");
	else if (parts.size() == 2) {
		bl->set_target(parts[1]);
		put_configuration_string(BLINKENLIGHTS_CFG_FILE, parts[1]);
		cnsl->set_blinkenlights_panel(bl);
	}
	else {
		put_configuration_string(BLINKENLIGHTS_CFG_FILE, "");
		cnsl->set_blinkenlights_panel(nullptr);
		cnsl->put_string_lf("PiDP11 blinkenlights panel disabled");
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_panel_brightness(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	cnsl->set_panel_brightness(std::min(127, std::stoi(parts[1])));
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_ddp(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	if (network_configured == false)
		cnsl->put_string_lf("Please configure network first (cfgnet)");
	else if (parts.size() == 3) {
		ddp_->set_target(parts[1], std::stoi(parts[2]));
		cnsl->set_ddp_panel(ddp_);
	}
	else {
		cnsl->set_ddp_panel(nullptr);
		cnsl->put_string_lf("DDP panel disabled (IP address and/or LED count missing)");
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_ramsize(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	if (parts.size() == 2)
		b->set_memory_size(std::stoi(parts.at(1)));
	else {
		int n_pages = b->getRAM()->get_memory_size() / 8192;
		cnsl->put_string_lf(format("Memory size: %u pages or %u kB (decimal)", n_pages, n_pages * 8192 / 1024));
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_cls(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	const char cls[] = { 27, '[', '2', 'J', 27, '[', 'H', 12, 0 };
	cnsl->put_string_lf(cls);
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_stats(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
#if defined(ESP32)
	cnsl->put_string_lf("ESP32");
	cnsl->put_string_lf(format("Free RAM (decimal bytes): %d", ESP.getFreeHeap()));
	cnsl->put_string_lf(format("Free SPI-RAM: %d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
#elif defined(BUILD_FOR_PICO2W)
	cnsl->put_string_lf("Pico(2)W");
	cnsl->put_string_lf(format("Free RAM (decimal bytes): %d", rp2040.getFreeHeap()));
	cnsl->put_string_lf(format("Clock frequency: %d", rp2040.f_cpu()));
#elif defined(TEENSY4_1)
	cnsl->put_string_lf("Teensy 4.1");
	// TODO
#elif IS_POSIX
	cnsl->put_string_lf("POSIX system");
	// TODO
#elif defined(_WIN32)
	cnsl->put_string_lf("WIN32 system");
#else
	cnsl->put_string_lf("?");
#endif
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_dir(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	ls_l(cnsl);
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_bic(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const c, debugger_state *const, kek_event_t *const)
{
	auto rc = load_tape(b, parts[1].c_str(), cnsl);
	if (rc.has_value()) {
		c->setPC(rc.value());
		cnsl->put_string_lf("BIC/LDA file loaded");
	}
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_lt(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	if (parts.size() == 2)
		tm11_load_tape(cnsl, b, parts[1]);
	else
		tm11_load_tape(cnsl, b, { });
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_ult(console *const, const std::vector<std::string> &, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	tm11_unload_tape(b);
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_dp(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	cnsl->stop_panel_thread();
	cnsl->put_string_lf("OK");
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_pm(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	console::panel_mode_t mode = console::PM_BITS;
	if (parts[1] == "bits") {
	}
	else if (parts[1] == "address1")
		mode = console::PM_ADDRESS1;
	else if (parts[1] == "address2") {
		mode = console::PM_ADDRESS2;
	}
	cnsl->set_panel_mode(mode);
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_refr(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	if (parts.size() == 2) {
		int rate = std::stoi(parts[1]);
		if (rate > 0)
			cnsl->set_refreshrate(rate);
	}
	cnsl->put_string_lf(format("Panel refresh rate: %d fps", cnsl->get_refreshrate()));
	return debugger_continue;
}

#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
FLASHMEM cmd_rc cmd_cfgnet(console *const cnsl, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	network_configured = true;
	if (parts.size() == 2)
		configure_network(cnsl, parts[1]);
	else
		configure_network(cnsl, { });
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_chknet(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	check_network(cnsl);
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_startnet(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	start_network(cnsl);
	network_configured = true;
	return debugger_continue;
}
#endif

FLASHMEM cmd_rc cmd_marker(console *const, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const state, kek_event_t *const)
{
	state->marker = !state->marker;
	return debugger_continue;
}

FLASHMEM cmd_rc cmd_log(console *const, const std::vector<std::string> & parts, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	std::string line;
	for(size_t i=1; i<parts.size(); i++) {
		if (i != 1)
			line += " ";
		line += parts[i];
	}
	DOLOG(log_ss::LS_GENERIC, line.c_str());
	return debugger_continue;
}

#if IS_POSIX
FLASHMEM cmd_rc cmd_ser(console *const cnsl, const std::vector<std::string> & parts, bus *const b, cpu *const, debugger_state *const, kek_event_t *const)
{
	serialize_state(cnsl, b, parts.at(1));
	return debugger_continue;
}
#endif

constexpr const cmd_pair cmd_pairs[] {
	{ "help", "", "this help", cmd_help, cmd_pair::par_no },
	{ "disassemble", "pc=/n=", "show current instruction", cmd_disassemble, cmd_pair::par_yes },
	{ "go", "", "run until trap or ^e", cmd_go, cmd_pair::par_no },
	{ "benchmark", "-v=verbose, -m=mmu", "determine the speed of the emulation", cmd_benchmark, cmd_pair::par_optional },
#if !defined(ESP32) || defined(BUILD_FOR_PICO2W)
	{ "quit", "", "stop emulator", cmd_quit, cmd_pair::par_no },
#endif
#if defined(BUILD_FOR_RP204o)
	{ "flash", "", "jump to the bootloader to allow flashing new firmware", cmd_flash, cmd_pair::par_no },
#endif
	{ "examine", "octal-address p|v [n]", "show memory address", cmd_examine, cmd_pair::par_yes },
	{ "reset", "which", "reset cpu/bus/etc", cmd_reset, cmd_pair::par_optional },
	{ "sbp", "", "set breakpoint(s), e.g.: action (pc=0123 and memwv[04000]=0200,0300 and (r4=07,05 or r5=0456) and instr[]=1), values seperated by ',', char after mem is w/b (word/byte), then follows v/p (virtual/physical), all octal values, mmr0-3 and psw are registers. \"action\" can be stop, trace or log. instr can have a mask between the [] and on the right an instruction-opcode to compare against.", cmd_sbp, cmd_pair::par_yes },
	{ "cbp", "", "clear breakpoints", cmd_cbp, cmd_pair::par_yes },
	{ "lbp", "", "list breakpoints", cmd_lbp, cmd_pair::par_no },
	{ "single", "[n]", "run 1 (or n-) instruction (implicit 'disassemble' command)", cmd_single, cmd_pair::par_optional },
	{ "trace", "con/fil", "toggle tracing for [con]sole/[fil]e logging", cmd_trace, cmd_pair::par_yes },
	{ "getlss", "con/fil", "show what subystems logging is enabled for", cmd_getlss, cmd_pair::par_yes },
	{ "list_ss", "", "list subsystems", cmd_list_ss, cmd_pair::par_no },
	{ "clss", "", "stop logging for all subsystems (use toggle_ss to re-enable)", cmd_clss, cmd_pair::par_optional },
	{ "toggle_ss", "con/fil x,[...]", "toggle logging for on or more subsystems", cmd_toggle_ss, cmd_pair::par_yes },
	{ "setsl", "hostname", "set syslog target", cmd_setsl, cmd_pair::par_yes },
	{ "pts", "setting", "enable (1) / disable (0) timestamps", cmd_pts, cmd_pair::par_yes },
	{ "turbo", "", "toggle turbo mode (cannot be interrupted)", cmd_turbo, cmd_pair::par_no },
	{ "state", "[reset [hard]] x", "dump state of (or reset) a device: rl02, rk05, rp06, rp07, mmu, tm11, kw11l, cpu, dc11, dz11 or deqna", cmd_state, cmd_pair::par_yes },
	{ "mmures", "x", "resolve a virtual address", cmd_mmures, cmd_pair::par_yes },
	{ "qi", "", "show queued interrupts", cmd_qi, cmd_pair::par_no },
	{ "setreg", "x y", "set register x to value y (octal)", cmd_setreg, cmd_pair::par_yes },
	{ "setpc", "pc", "set PC to value (octal)", cmd_setpc, cmd_pair::par_yes },
	{ "setstack", "x y", "set stack register x to value y (octal)", cmd_setstack, cmd_pair::par_yes },
	{ "setpsw", "y", "set PSW value y (octal)", cmd_setpsw, cmd_pair::par_yes },
	{ "deposit", "x y", "set memory x to value y, octal word", cmd_deposit, cmd_pair::par_yes },
	{ "setmem", "a v", "set memory (a=) to value (v=), both in octal, one byte", cmd_setmem, cmd_pair::par_yes },
	{ "getmem", "a", "get memory (a=), in octal, one byte", cmd_getmem, cmd_pair::par_yes },
	{ "toggle", "s t", "set switch (s=, 0...15 (decimal)) of the front panel to state (t=, 0 or 1)", cmd_toggle, cmd_pair::par_yes },
	{ "pcmon", "x", "track for x cycles what memory addresses were read an instruction from", cmd_pcmon, cmd_pair::par_yes },
	{ "deqna", "x[,y,z]", "set deqna emulation to use (x): \"linux\" (tap), \"teensy4.1\", \"esp32\" or \"vxlan\" (with host (y) & port (z))", cmd_deqna, cmd_pair::par_yes },
	{ "test", "x", "test the dz11/deqna/panel emulation, -r to continue until ^e is pressed, -d x: sleep x ms between each iteration", cmd_test, cmd_pair::par_yes },
	{ "mdeqna", "mode", "set DEQNA monitor mode: none, filtered, ll-trace, everything", cmd_mdeqna, cmd_pair::par_yes },
	{ "cfgdisk", "", "configure disk", cmd_cfgdisk, cmd_pair::par_no },
	{ "cdz11", "", "configure DZ11 device", cmd_cdz11, cmd_pair::par_no },
	{ "setinthz", "freq", "set KW11-L interrupt frequency (Hz)", cmd_setinthz, cmd_pair::par_yes },
	{ "getinthz", "", "get KW11-L interrupt frequency (Hz)", cmd_getinthz, cmd_pair::par_no },
	{ "blights", "ip-addr", "enable blinkenlights panel on selected IP address", cmd_blights, cmd_pair::par_yes },
	{ "ddp", "ip-addr LED_count", "enable ddp panel on selected IP address for LED_count LEDs", cmd_ddp, cmd_pair::par_yes },
	{ "pbright", "brightness", "set panel brightness (1-127)", cmd_panel_brightness, cmd_pair::par_yes },
	{ "dp", "", "disable panel", cmd_dp, cmd_pair::par_no },
	{ "pm", "mode", "panel mode (bits, address1 or address2)", cmd_pm, cmd_pair::par_yes },
	{ "refr", "fps", "set panel refreshrate", cmd_refr, cmd_pair::par_yes },
	{ "ramsize", "pages", "set ram size (page (8 kB) count, decimal)", cmd_ramsize, cmd_pair::par_yes },
	{ "cls", "", "clear screen", cmd_cls, cmd_pair::par_no },
	{ "stats", "", "show run statistics", cmd_stats, cmd_pair::par_no },
	{ "dir", "", "list files", cmd_dir, cmd_pair::par_no },
	{ "bic", "filename", "run BIC/LDA file", cmd_bic, cmd_pair::par_yes },
	{ "lt", "filename", "load tape", cmd_lt, cmd_pair::par_yes },
	{ "ult", "", "unload tape", cmd_ult, cmd_pair::par_no },
#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
	{ "cfgnet", "", "configure network (e.g. WiFi)", cmd_cfgnet, cmd_pair::par_no },
	{ "startnet", "", "start network", cmd_startnet, cmd_pair::par_no },
	{ "chknet", "", "check network status", cmd_chknet, cmd_pair::par_no },
#endif
	{ "marker", "", "toggle marker line in logging", cmd_marker, cmd_pair::par_no },
	{ "log", "", "log a message to the logfile", cmd_log, cmd_pair::par_optional },
#if IS_POSIX
	{ "ser", "filename", "serialize state to a file (deserialize with -D commandline parameter)", cmd_ser, cmd_pair::par_yes },
	// { "dser", "deserialize state from a file",         ^^^^ },
#endif
	{ nullptr, nullptr, nullptr, nullptr, cmd_pair::par_no }
};

std::optional<std::string> explode(console *const cnsl, const std::string & in)
{
	if (in.empty())
		return "";

	auto last_space = in.rfind(' ');
	if (last_space == std::string::npos)
		last_space = 0;
	else
		last_space++;
	std::string match_against     = in.substr(last_space);
	size_t      match_against_len = match_against.size();
	if (match_against_len == 0)
		return "";

	std::vector<const cmd_pair *> matches;

	size_t n = 0;
	do {
		std::string cur = cmd_pairs[n].command;
		if (cur.substr(0, match_against_len) == match_against)
			matches.push_back(&cmd_pairs[n]);
	} while(cmd_pairs[++n].command);

	if (matches.empty())
		return "";

	if (matches.size() == 1)
		return std::string(matches[0]->command).substr(match_against_len);

	cnsl->put_string_lf("");
	for(size_t i=0; i<matches.size(); i++) {
		if (i < matches.size() - 1) {
			cnsl->put_string(matches[i]->command);
			cnsl->put_string(", ");
		}
		else {
			cnsl->put_string_lf(matches[i]->command);
		}
	}

	return { };
}

FLASHMEM cmd_rc cmd_help(console *const cnsl, const std::vector<std::string> &, bus *const, cpu *const, debugger_state *const, kek_event_t *const)
{
	size_t max_cmd_len   = 0;
	size_t max_pars_len  = 0;
	size_t max_descr_len = 0;
	size_t n             = 0;
	do {
		max_cmd_len   = std::max(max_cmd_len,   strlen(cmd_pairs[n].command   ));
		max_pars_len  = std::max(max_pars_len,  strlen(cmd_pairs[n].parameters));
		max_descr_len = std::max(max_descr_len, strlen(cmd_pairs[n].descr     ));
	}
	while(cmd_pairs[++n].command);

	const size_t scr_width = term_cols;

	for(size_t i=0; i<n; i++) {
		cnsl->put_string(format("%-*s - %-*s - ", max_cmd_len, cmd_pairs[i].command, max_pars_len, cmd_pairs[i].parameters));
		std::string descr = cmd_pairs[i].descr;
		const size_t indent    = max_cmd_len + max_pars_len + 3 + 3;
		const size_t max_width = scr_width - indent;
		      size_t skip      = 0;
		do {
			auto        space  = descr.size() > max_width ? descr.rfind(' ', max_width) : 0;
			std::string substr = format("%-*s", skip, "");
			if (space != std::string::npos && space > 4) {
				substr += descr.substr(0, space);
				descr   = descr.substr(space + 1);
			}
			else {
				auto left = std::min(max_width, descr.size());
				substr += descr.substr(0, left);
				descr   = descr.substr(left);
			}
			cnsl->put_string_lf(substr);
			skip    = indent;
		}
		while(descr.empty() == false);
	}

	return debugger_continue;
}

bool emulation_do(console *const cnsl, bus *const b, cpu *const c, debugger_state *const state, kek_event_t *const stop_event)
{
	*cnsl->get_running_flag() = true;

	if (state->turbo) {
		while(load_relaxed_p(stop_event) == EVENT_NONE)
			c->step();
	}
	else {
		uint64_t total_wait_duration = 0;
		uint64_t wait_count          = 0;
		uint16_t last_pc             = 0;
		uint64_t since               = get_us();
		uint64_t took                = 0;
		uint64_t start_trap_count    = c->get_trap_counter();
		bool     is_trace_enabled    = trace_enabled();
		std::unordered_map<uint16_t, uint32_t> trap_counts_before = c->get_trap_counts();
		while(load_relaxed_p(stop_event) == EVENT_NONE) {
			if (is_trace_enabled || state->single_step) {
				if (!state->single_step)
					DOLOG(log_ss::LS_TRACE, "---");

				auto rc = disassemble(c, state->single_step ? cnsl : nullptr, c->getPC(), false);
				took += std::get<1>(rc);
				DOLOG(log_ss::LS_TRACE, "%s", std::get<3>(rc).c_str());
			}

			auto bp_result = c->check_breakpoint();
			if (bp_result.has_value()) {
				DOLOG(log_ss::LS_TRACE, "Breakpoint: %s", bp_result.value().second.c_str());
				if (bp_result.value().first.get_action() == breakpoint::bp_action::stop_running) {
					cnsl->put_string_lf("Breakpoint: " + bp_result.value().second);
					if (!state->single_step)
						break;
				}
				else if (bp_result.value().first.get_action() == breakpoint::bp_action::start_tracing) {
					set_ss_log(false, log_ss::LS_TRACE);
					set_ss_log(true,  log_ss::LS_TRACE);
				}
				else if (bp_result.value().first.get_action() == breakpoint::bp_action::only_log_entry) {
					DOLOG(log_ss::LS_TRACE, "Breakpoint: %s", bp_result.value().second.c_str());
				}
			}

			auto pc = c->getPC();
			if (state->pc_monitor_counter != state->pc_monitor_count) {
				state->pc_monitor_counter++;
				last_pc = pc;
				auto it = state->pc_monitor.find(pc);
				if (it != state->pc_monitor.end())
					it->second.first++;
				else
					state->pc_monitor.insert({ pc, { 1, state->pc_monitor_counter } });
			}

			uint16_t instr = b->read_word(pc);
			if (instr == 0) {  // WAIT
				uint64_t wait_start = get_us();
				c->step();
				uint64_t wait_end = get_us();
				total_wait_duration += wait_end - wait_start;
				wait_count++;
			}
			else {
				c->step();
			}

			if (state->single_step && --state->n_single_step == 0)
				break;

			if (state->pc_monitor_enabled && state->pc_monitor_counter == state->pc_monitor_count)
				break;
		}
		uint64_t end_trap_count = c->get_trap_counter();
		std::unordered_map<uint16_t, uint32_t> trap_counts_after = c->get_trap_counts();

		if (state->pc_monitor_enabled) {
			std::map<int, std::pair<uint16_t, uint32_t> > ordered;
			for(auto & entry: state->pc_monitor)
				ordered.insert({ entry.second.second, { entry.first, entry.second.first } });
			state->pc_monitor.clear();
			unsigned expected_count = state->pc_monitor_counter / ordered.size();
			for(auto & entry: ordered) {
				auto rc = disassemble(c, nullptr, entry.second.first, true);
				cnsl->put_string_lf(format("%5d] %4u %c%c%c\t%s", entry.first, entry.second.second,
							entry.second.second > expected_count ? '*' : ' ',
							entry.second.first == last_pc ? 'L' : ' ',
							std::get<2>(rc) ? 'I': ' ',
							std::get<3>(rc).c_str()));
			}
			cnsl->put_string_lf(format("%" PRIzu " counters in %u instructions", ordered.size(), state->pc_monitor_count));
		}

		if (trace_enabled() || state->go_verbose || state->pc_monitor_enabled) {
			cnsl->put_string_lf(format("Took %.3f emulated ms, %.3f s wall clock time, avg. wait duration: %.3f us, traps: %" PRIzu "",
						took / 1000000.,  // took is nanoseconds
						(get_us() - since) / 1000000.,
						wait_count > 0 ? total_wait_duration / double(wait_count) : 0,
					size_t(end_trap_count - start_trap_count)));
			for(auto & trap : trap_counts_after) {
				uint32_t cnt = trap.second; 
				auto     it  = trap_counts_before.find(trap.first);
				if (it != trap_counts_before.end())
					cnt -= it->second;
				cnsl->put_string_lf(format("trap vec %06o: %u", trap.first, cnt));
			}
		}

		state->pc_monitor_enabled = false;
	}

	*cnsl->get_running_flag() = false;

	return true;
}

FLASHMEM bool debugger_do(debugger_state *const state, console *const cnsl, bus *const b, kek_event_t *const stop_event, const std::string & cmd)
{
	cpu  *const c = b->getCpu();
	auto parts    = split(cmd, " ");
	if (parts.empty())
		return true;

	size_t i = 0;
	do {
		auto & item = cmd_pairs[i];
		if (std::string(item.command) == parts[0]) {
			if ((parts.size() == 1 && item.par_t == cmd_pair::par_no ) ||
			    (parts.size() >  1 && item.par_t == cmd_pair::par_yes) ||
			    item.par_t == cmd_pair::par_optional) {
				auto rc = item.func(cnsl, parts, b, c, state, stop_event);
				if (rc == debugger_continue)
					return true;
				if (rc == debugger_stop)
					return false;
				return emulation_do(cnsl, b, c, state, stop_event);
			}

			cnsl->put_string_lf("Invalid number of parameters");
			return true;
		}
	}
	while(cmd_pairs[++i].command);

	cnsl->put_string_lf(format("Command %s is unknown", parts[0].c_str()));
	return true;
}

FLASHMEM void debugger(console *const cnsl, bus *const b, kek_event_t *const stop_event, const std::optional<std::string> & init)
{
	debugger_state state;

	b->set_debug_mode();

	if (init.has_value()) {
		std::string   line;
		std::ifstream fh;
		fh.open(init.value());
		while(std::getline(fh, line)) {
			try {
				if (debugger_do(&state, cnsl, b, stop_event, line) == false)
					return;
			}
			catch(...) {
				cnsl->put_string_lf("Exception in debugger");
			}
		}
	}

	while(load_relaxed_p(stop_event) != EVENT_TERMINATE) {
		try {
			if (state.marker)
				cnsl->put_string_lf("---");

			std::string cmd = cnsl->read_line(std::to_string(int(load_relaxed_p(stop_event))), explode);
			if (debugger_do(&state, cnsl, b, stop_event, cmd) == false)
				break;
		}
		catch(const std::exception & e) {
			cnsl->put_string_lf(format("Exception caught: %s", e.what()));
		}
		catch(const int ei) {
			cnsl->put_string_lf(format("Problem: %d", ei));
		}
		catch(...) {
			cnsl->put_string_lf("Unknown exception caught");
		}
	}

	*stop_event = EVENT_TERMINATE;
}

FLASHMEM void simple_run(console *const cnsl, bus *const b, kek_event_t *const stop_event)
{
	cpu  *const c = b->getCpu();
	bool        t = trace_enabled();

	*cnsl->get_running_flag() = true;

	while(*stop_event == EVENT_NONE) {
		if (t) {
			auto rc = disassemble(c, nullptr, c->getPC(), false);
			DOLOG(log_ss::LS_TRACE, "%s", std::get<3>(rc).c_str());
			DOLOG(log_ss::LS_TRACE, "---");
		}

		c->step();
	}

	*cnsl->get_running_flag() = false;
}
