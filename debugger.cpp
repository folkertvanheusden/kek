// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <optional>
#include "gen.h"
#if IS_POSIX || defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <Arduino.h>
#include <LittleFS.h>
#endif

#include "breakpoint_parser.h"
#include "bus.h"
#if IS_POSIX
#include "comm_posix_tty.h"
#endif
#include "comm_tcp_socket_client.h"
#include "comm_tcp_socket_server.h"
#include "console.h"
#include "cpu.h"
#if defined(ESP32)
#include "comm_esp32_hardwareserial.h"
#endif
#include "disk_backend.h"
#if IS_POSIX || defined(_WIN32)
#include "disk_backend_file.h"
#else
#include "disk_backend_esp32.h"
#endif
#include "disk_backend_nbd.h"
#include "kw11-l.h"
#include "loaders.h"
#include "log.h"
#include "memory.h"
#include "tty.h"
#include "utils.h"


#if defined(ESP32) || defined(BUILD_FOR_RP2040)
#if defined(ESP32)
#include "esp32.h"
#include "console_esp32.h"
#elif defined(BUILD_FOR_RP2040)
#include "rp2040.h"
#endif

void configure_network(console *const cnsl);
void check_network(console *const cnsl);
void start_network(console *const cnsl);
#endif

#if !defined(BUILD_FOR_RP2040) && !defined(linux) && !defined(_WIN32)
extern SdFs SD;
#endif

#define SERIAL_CFG_FILE "dc11.json"

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

	disk_backend *d = new disk_backend_nbd(hostname, std::stoi(port_str));
	if (d->begin(false) == false) {
		cnsl->put_string_lf("Cannot initialize NBD client");
		delete d;
		return { };
	}

	return d;
}
#endif

void start_disk(console *const cnsl)
{
#if IS_POSIX
	return;
#else
	static bool disk_started = false;
	if (disk_started)
		return;

#if defined(ESP32)
	cnsl->put_string_lf(format("MISO: %d", int(MISO)));
	cnsl->put_string_lf(format("MOSI: %d", int(MOSI)));
	cnsl->put_string_lf(format("SCK : %d", int(SCK )));
	cnsl->put_string_lf(format("SS  : %d", int(SS  )));

#endif

#if defined(ESP32_WT_ETH01)
	if (SD.begin(SdioConfig(FIFO_SDIO)))
		disk_started = true;
#elif defined(SHA2017)
	if (SD.begin(21, SD_SCK_MHZ(10)))
		disk_started = true;
#elif !defined(BUILD_FOR_RP2040)
	if (SD.begin(SS, SD_SCK_MHZ(15)))
		disk_started = true;
#endif
	if (!disk_started) {
		auto err = SD.sdErrorCode();
		if (err)
			cnsl->put_string_lf(format("SDerror: 0x%x, data: 0x%x", err, SD.sdErrorData()));
		else
			cnsl->put_string_lf("Failed to initialize SD card");
	}
#endif
}

void ls_l(console *const cnsl)
{
	start_disk(cnsl);

#if IS_POSIX || defined(_WIN32)
	cnsl->put_string_lf("Files in current directory: ");
#else
	cnsl->put_string_lf("Files on SD-card:");
#endif

#if defined(linux)
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
#elif defined(BUILD_FOR_RP2040)
	File root = SD.open("/");

	for(;;) {
		auto entry = root.openNextFile();
		if (!entry)
			break;

		if (!entry.isDirectory()) {
			cnsl->put_string(entry.name());
			cnsl->put_string("\t\t");
			cnsl->put_string_lf(format("%ld", entry.size()));
		}

		entry.close();
	}
#elif defined(_WIN32)
#else
	SD.ls("/", LS_DATE | LS_SIZE | LS_R);
#endif
}

std::optional<std::string> select_host_file(console *const cnsl)
{
	for(;;) {
		cnsl->flush_input();

		std::string selected_file = cnsl->read_line("Enter filename (or empty to abort): ");

		if (selected_file.empty())
			return { };

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

		ls_l(cnsl);
	}
}

// disk image files
std::optional<disk_backend *> select_disk_file(console *const cnsl)
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

void configure_comm(console *const cnsl, std::vector<comm *> & device_list)
{
	for(;;) {
		std::vector<char> keys_allowed { '9' };
		int               slot_key     { 'A' };
		for(auto & c: device_list) {
			cnsl->put_string_lf(format(" %c. %s", slot_key, c ? c->get_identifier().c_str() : "-"));
			keys_allowed.push_back(slot_key);
			slot_key++;
		}

		int ch_dev = wait_for_key("Select communication device to setup or 9. to exit", cnsl, keys_allowed);
		if (ch_dev == '9')
			break;

		size_t device_nr = ch_dev - 'A';

		int  ch_opt = wait_for_key("1. TCP client, 2. TCP server, 3. serial device, 9. to abort", cnsl, { '1', '2', '3', '9' });
		bool rc     = false;

		if (ch_opt == '1') {
			std::string temp_host = cnsl->read_line("host: ");
			std::string temp_port = temp_host.empty() ? "" : cnsl->read_line("port: ");

			if (temp_host.empty() == false && temp_port.empty() == false) {
				delete device_list.at(device_nr);
				device_list.at(device_nr) = new comm_tcp_socket_client(temp_host, std::stoi(temp_port));
				rc = device_list.at(device_nr)->begin();
			}
		}
		else if (ch_opt == '2') {
			std::string temp = cnsl->read_line("port: ");
			if (temp.empty() == false) {
				delete device_list.at(device_nr);
				device_list.at(device_nr) = new comm_tcp_socket_server(std::stoi(temp));
				rc = device_list.at(device_nr)->begin();
			}
		}
		else if (ch_opt == '3') {
#if IS_POSIX
			std::string temp_dev = cnsl->read_line("device: ");
			std::string temp_bitrate = cnsl->read_line("bitrate: ");
			if (temp_dev.empty() == false && temp_bitrate.empty() == false) {
				delete device_list.at(device_nr);
				device_list.at(device_nr) = new comm_posix_tty(temp_dev, std::stoi(temp_bitrate));
				rc = device_list.at(device_nr)->begin();
			}
#elif defined(ESP32)
			std::string temp_dev = cnsl->read_line("Uart number (0...2): ");
			std::string temp_rx  = cnsl->read_line("RX pin: ");
			std::string temp_tx  = cnsl->read_line("TX pin: ");
			std::string temp_bitrate = cnsl->read_line("bitrate: ");
			if (temp_dev.empty() == false && temp_bitrate.empty() == false && temp_rx.empty() == false && temp_tx.empty() == false) {
				delete device_list.at(device_nr);
				device_list.at(device_nr) = new comm_esp32_hardwareserial(std::stoi(temp_dev), std::stoi(temp_rx), std::stoi(temp_tx), std::stoi(temp_bitrate));
				rc = device_list.at(device_nr)->begin();
			}
#else
			cnsl->put_string_lf("Not implemented yet");
#endif
		}

		if (ch_opt != 9 && rc == false)
			cnsl->put_string_lf("Failed to initialize device");
	}
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
	int type_ch = wait_for_key("1. RK05, 2. RL02, 3. RP06, 9. abort", cnsl, { '1', '2', '3', '9' });

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
	else if (type_ch == '3') {
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
	if (data.empty())
		return 2;  // problem!

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

	auto data     = b->getMMU()->calculate_physical_address(run_mode, va);

	uint16_t page_offset = va & 8191;
	cnsl->put_string_lf(format("Active page field: %d, page offset: %o (%d)", data.apf, page_offset, page_offset));
	cnsl->put_string_lf(format("Phys. addr. instruction: %08o (psw: %d)", data.physical_instruction, data.physical_instruction_is_psw));
	cnsl->put_string_lf(format("Phys. addr. data: %08o (psw: %d)", data.physical_data, data.physical_data_is_psw));

	uint16_t mmr3 = b->getMMU()->getMMR3();

	if (run_mode == 0) {
		b->getMMU()->dump_par_pdr(cnsl, 1, false, "supervisor i-space", 0,                  data.apf);
		b->getMMU()->dump_par_pdr(cnsl, 1, true,  "supervisor d-space", 1 + (!!(mmr3 & 4)), data.apf);
	}
	else if (run_mode == 1) {
		b->getMMU()->dump_par_pdr(cnsl, 0, false, "kernel i-space",     0,                  data.apf);
		b->getMMU()->dump_par_pdr(cnsl, 0, true,  "kernel d-space",     1 + (!!(mmr3 & 4)), data.apf);
	}
	else if (run_mode == 3) {
		b->getMMU()->dump_par_pdr(cnsl, 3, false, "user i-space",       0,                  data.apf);
		b->getMMU()->dump_par_pdr(cnsl, 3, true,  "user d-space",       1 + (!!(mmr3 & 4)), data.apf);
	}

	for(int i=0; i<2; i++) {
		auto ta_i = b->getMMU()->get_trap_action(run_mode, false, data.apf, i);
		auto ta_d = b->getMMU()->get_trap_action(run_mode, true,  data.apf, i);

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

void serialize_state(console *const cnsl, const bus *const b, const std::string & filename)
{
	JsonDocument j = b->serialize();

	bool ok = false;

	FILE *fh = fopen(filename.c_str(), "w");
	if (fh) {
		state_writer ws { fh };
		serializeJsonPretty(j, ws);
		fclose(fh);

		ok = true;
	}

	cnsl->put_string_lf(format("Serialize to %s: %s", filename.c_str(), ok ? "OK" : "failed"));
}

void tm11_load_tape(console *const cnsl, bus *const b, const std::optional<std::string> & file)
{
	if (file.has_value())
		b->getTM11()->load(file.value());
	else {
		auto sel_file = select_host_file(cnsl);

		if (sel_file.has_value())
			b->getTM11()->load(sel_file.value());
	}
}

void tm11_unload_tape(bus *const b)
{
	b->getTM11()->unload();
}

void serdc11(console *const cnsl, bus *const b)
{
	dc11         *d = b->getDC11();
	if (!d) {
		cnsl->put_string_lf("No DC11 configured");
		return;
	}

	JsonDocument j = d->serialize();

	bool ok = false;

#if IS_POSIX
	FILE *fh = fopen(SERIAL_CFG_FILE, "w");
	if (fh) {
		state_writer ws { fh };
		serializeJsonPretty(j, ws);
		fclose(fh);

		ok = true;
	}
#elif defined(ESP32)
	File data_file = LittleFS.open("/" SERIAL_CFG_FILE, "w");
	if (data_file) {
		serializeJsonPretty(j, data_file);
		data_file.close();

		ok = true;
	}
#endif

	cnsl->put_string_lf(format("Serialize to " SERIAL_CFG_FILE ": %s", ok ? "OK" : "failed"));
}

void deserdc11(console *const cnsl, bus *const b)
{
#if defined(ESP32)
	auto rc = deserialize_file("/" SERIAL_CFG_FILE);
#else
	auto rc = deserialize_file(SERIAL_CFG_FILE);
#endif
	if (rc.has_value() == false) {
		cnsl->put_string_lf("Failed to deserialize " SERIAL_CFG_FILE);
		return;
	}

	b->del_DC11();

	b->add_DC11(dc11::deserialize(rc.value(), b));

	cnsl->put_string_lf(format("Deserialized " SERIAL_CFG_FILE));
}

void set_kw11_l_interrupt_freq(console *const cnsl, bus *const b, const int freq)
{
	if (freq >= 1 && freq < 1000)
		b->getKW11_L()->set_interrupt_frequency(freq);
	else
		cnsl->put_string_lf("Frequency out of range");
}

void debugger(console *const cnsl, bus *const b, std::atomic_uint32_t *const stop_event)
{
	int32_t trace_start_addr = -1;
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
					n_single_step = std::stoi(parts[1]);
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
				settrace(!gettrace());

				cnsl->put_string_lf(format("Tracing set to %s", gettrace() ? "ON" : "OFF"));

				continue;
			}
			else if (parts[0] == "state") {
				if (parts[1] == "rl02")
					b->getRL02()->show_state(cnsl);
				else if (parts[1] == "mmu")
					b->getMMU() ->show_state(cnsl);
				else if (parts[1] == "rk05")
					b->getRK05()->show_state(cnsl);
				else if (parts[1] == "dc11")
					b->getDC11()->show_state(cnsl);
				else if (parts[1] == "tm11")
					b->getTM11()->show_state(cnsl);
				else if (parts[1] == "kw11l")
					b->getKW11_L()->show_state(cnsl);
				else if (parts[1] == "rp06")
					b->getRP06()->show_state(cnsl);
				else
					cnsl->put_string_lf(format("Device \"%s\" is not known", parts[1].c_str()));

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
					int      n    = parts.size() == 4 ? std::stoi(parts[3]) : 1;

					if (parts[2] != "p" && parts[2] != "v") {
						cnsl->put_string_lf("expected p (physical address) or v (virtual address)");

						continue;
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
			else if (parts[0] == "pm" && parts.size() == 2) {
				reinterpret_cast<console_esp32 *>(cnsl)->set_panel_mode(parts[1] == "bits" ? console_esp32::PM_BITS : console_esp32::PM_POINTER);

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
					int n_pages = b->getRAM()->get_memory_size() / 8192;

					cnsl->put_string_lf(format("Memory size: %u pages or %u kB (decimal)", n_pages, n_pages * 8192 / 1024));
				}

				continue;
			}
			else if (parts[0] == "bl" && parts.size() == 2) {
				if (parts.at(1) == "rk05")
					set_boot_loader(b, BL_RK05);
				else if (parts.at(1) == "rl02")
					set_boot_loader(b, BL_RL02);
				else if (parts.at(1) == "rp06")
					set_boot_loader(b, BL_RP06);
				else
					cnsl->put_string_lf("???");

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
			else if (parts[0] == "setinthz" && parts.size() == 2) {
				set_kw11_l_interrupt_freq(cnsl, b, std::stoi(parts.at(1)));
				continue;
			}
			else if (parts[0] == "setsl" && parts.size() == 3) {
				if (setloghost(parts.at(1).c_str(), parse_ll(parts[2])) == false)
					cnsl->put_string_lf("Failed parsing IP address");
				else
					send_syslog(info, "Hello, world!");

				continue;
			}
			else if (parts[0] == "pts" && parts.size() == 2) {
				cnsl->enable_timestamp(std::stoi(parts[1]));

				continue;
			}
			else if (cmd == "qi") {
				show_queued_interrupts(cnsl, c);

				continue;
			}
			else if (parts[0] == "log") {
				DOLOG(info, true, cmd.c_str());

				continue;
			}
			else if (parts[0] == "bic" && parts.size() == 2) {
				auto rc = load_tape(b, parts[1].c_str());
				if (rc.has_value()) {
					c->setPC(rc.value());

					cnsl->put_string_lf("BIC/LDA file loaded");
				}
				else {
					cnsl->put_string_lf("BIC/LDA failed to load");
				}

				continue;
			}
			else if (parts[0] == "lt") {
				if (parts.size() == 2)
					tm11_load_tape(cnsl, b, parts[1]);
				else
					tm11_load_tape(cnsl, b, { });

				continue;
			}
			else if (cmd == "dir" || cmd == "ls") {
				ls_l(cnsl);

				continue;
			}
			else if (cmd == "ult") {
				tm11_unload_tape(b);

				continue;
			}
			else if (parts[0] == "testdc11") {
				b->getDC11()->test_ports(cmd);

				continue;
			}
			else if (cmd == "dp") {
				cnsl->stop_panel_thread();

				continue;
			}
			else if (cmd == "cdc11") {
				configure_comm(cnsl, *b->getDC11()->get_comm_interfaces());

				continue;
			}
			else if (cmd == "serdc11") {
				serdc11(cnsl, b);

				continue;
			}
			else if (cmd == "dserdc11") {
				deserdc11(cnsl, b);

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
					"setll x,y     - set loglevel: terminal,file",
					"setsl x,y     - set syslog target: requires a hostname and a loglevel",
					"pts x         - enable (1) / disable (0) timestamps",
					"turbo         - toggle turbo mode (cannot be interrupted)",
					"debug         - enable CPU debug mode",
					"bt            - show backtrace - need to enable debug first",
					"strace x      - start tracing from address - invoke without address to disable",
					"trl x         - set trace run-level (0...3), empty for all",
					"regdump       - dump register contents",
					"state x       - dump state of a device: rl02, rk05, rp06, mmu, tm11, kw11l or dc11",
					"mmures x      - resolve a virtual address",
					"qi            - show queued interrupts",
					"setpc x       - set PC to value",
					"setmem ...    - set memory (a=) to value (v=), both in octal, one byte",
					"toggle ...    - set switch (s=, 0...15 (decimal)) of the front panel to state (t=, 0 or 1)",
					"setinthz x    - set KW11-L interrupt frequency (Hz)",
					"cls           - clear screen",
					"dir           - list files",
					"bic x         - run BIC/LDA file",
					"lt x          - load tape (parameter is filename)",
					"ult           - unload tape",
					"stats         - show run statistics",
					"ramsize x     - set ram size (page (8 kB) count, decimal)",
					"bl            - set bootloader (rl02, rk05 or rp06)",
					"cdc11         - configure DC11 device",
					"serdc11       - store DC11 device settings",
					"dserdc11      - load DC11 device settings",
#if IS_POSIX
					"ser x         - serialize state to a file",
//					"dser          - deserialize state from a file",
#endif
					"dp            - disable panel",
#if defined(ESP32)
					"cfgnet        - configure network (e.g. WiFi)",
					"startnet      - start network",
					"chknet        - check network status",
					"pm x          - panel mode (bits or address)",
#endif
					"testdc11      - test DC11",
					"cfgdisk       - configure disk",
					"log ...       - log a message to the logfile",
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
					if (trace_start_addr != -1 && c->getPC() == trace_start_addr)
						settrace(true);

					if ((gettrace() || single_step) && (t_rl.has_value() == false || t_rl.value() == c->getPSW_runmode())) {
						if (!single_step)
							TRACE("---");

						disassemble(c, single_step ? cnsl : nullptr, c->getPC(), false);
					}

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

void run_bic(console *const cnsl, bus *const b, std::atomic_uint32_t *const stop_event, const uint16_t start_addr)
{
	cpu *const c = b->getCpu();

	c->set_register(7, start_addr);

	*cnsl->get_running_flag() = true;

	while(*stop_event == EVENT_NONE) {
		if (gettrace())
			disassemble(c, nullptr, c->getPC(), false);

		c->step();
	}

	*cnsl->get_running_flag() = false;
}
