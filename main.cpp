// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include <atomic>
#include <cassert>
#include <cinttypes>
#include <signal.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <unistd.h>

#include "blinkenlights.h"
#include "error.h"
#include "comm.h"
#include "comm_posix_tty.h"
#include "comm_tcp_socket_server.h"
#if defined(USE_IMGUI)
#include "console_imgui.h"
#endif
#if defined(_WIN32)
#include "win32.h"
#else
#include "console_ncurses.h"
#endif
#include "console_comm.h"
#include "console_posix.h"
#include "cpu.h"
#include "ddp.h"
#include "debugger.h"
#include "deqna.h"
#include "disk_backend.h"
#include "disk_backend_file.h"
#include "disk_backend_nbd.h"
#include "dc11.h"
#include "dz11.h"
#include "eth_transport.h"
#include "eth_transport_linux.h"
#include "eth_transport_vxlan.h"
#include "gen.h"
#include "kw11-l.h"
#include "loaders.h"
#include "log.h"
#include "memory.h"
#if !defined(_WIN32)
#include "terminal.h"
#endif
#include "tty.h"
#include "utils.h"


std::atomic_uint32_t event   { 0                 };
std::atomic_bool    *running { nullptr           };
blinkenlights       *bl      { new blinkenlights };
ddp                 *ddp_    { new ddp           };

std::atomic_bool  sigw_event { false             };

constexpr const uint16_t validation_psw_mask = 0174037;  // ignore unused bits & priority(!)
constexpr const int      default_port_offset = 1100;

void sw_handler(int s)
{
#if defined(_WIN32)
	fprintf(stderr, "Terminating...\n");
	event = EVENT_TERMINATE;
#else
	if (s == SIGWINCH)
		sigw_event = true;
	else {
		fprintf(stderr, "Terminating...\n");

		event = EVENT_TERMINATE;
	}
#endif
}

#if defined(JANSSON)
#include <jansson.h>

std::vector<std::pair<uint32_t, uint8_t> > get_memory_settings(const json_t *const ja)
{
	std::vector<std::pair<uint32_t, uint8_t> > out;
	size_t array_size = json_array_size(ja);
	for(size_t i=0; i<array_size; i++) {
		json_t *kv_dict = json_array_get(ja, i);

		const char *key   { nullptr };
		json_t     *value { nullptr };
		// should be one element
		json_object_foreach(kv_dict, key, value) {
			uint32_t a = std::stoi(key, nullptr, 8);
			uint16_t v = json_integer_value(json_object_get(kv_dict, key));
			out.push_back({ a, v });
			if (a == 0 && v == 0)
				printf("Suspect\n");
		}
	}
	return out;
}

bool compare_values(console *const, uint32_t v, uint32_t should_be, const std::string & name)
{
	if (v == should_be)
		return true;

	int different_bits = v ^ should_be;
	std::string different_bits_str;
	do {
		different_bits_str = std::to_string(different_bits & 1) + different_bits_str;
	} while(different_bits >>= 1);

	DOLOG(log_ss::LS_TRACE, "%s: %o (is) != %o (should be), different bits: %s", name.c_str(), v, should_be, different_bits_str.c_str());

	return false;
}

int run_cpu_validation(console *const cnsl, const std::string & filename)
{
	DOLOG(log_ss::LS_TRACE, "run_cpu_validation(%s)", filename.c_str());

	json_error_t error { };
	json_t      *doc = json_load_file(filename.c_str(), 0, &error);
	if (!doc)
		return { };

	int n_tests                = 0;
	int total_error_count      = 0;
	int tests_with_error_count = 0;

	size_t array_len = json_array_size(doc);
	for(size_t el_nr = 0; el_nr<array_len; el_nr++) {
		json_t *element = json_array_get(doc, el_nr);

		n_tests++;

		DOLOG(log_ss::LS_TRACE, "--- test %d start ---", n_tests);

		// create environment
		event = 0;
		bus *b = new bus();
		b->set_memory_size(DEFAULT_N_PAGES);
		cpu *c = new cpu(b, &event);
		b->add_cpu(c);

		// SET
		json_t *before = json_object_get(element, "before");
		{
			// PC
			c->setPC(json_integer_value(json_object_get(before, "PC")));

			// PSW
			c->setPSW(json_integer_value(json_object_get(before, "PSW")), false);

			// stackpointers
			for(int i=0; i<4; i++)
				c->set_stackpointer(i, json_integer_value(json_object_get(before, format("stack-%d", i).c_str())));

			b->getMMU()->setMMR1(json_integer_value(json_object_get(before, "mmr1")));
			b->getMMU()->setMMR2(json_integer_value(json_object_get(before, "mmr2")));

			// registers
			for(int set=0; set<2; set++) {
				for(int i=0; i<6; i++)
					c->lowlevel_register_set(set, i, json_integer_value(json_object_get(before, format("reg-%d.%d", i, set).c_str())));
			}

			// memory
			json_t *memory_before = json_object_get(before, "memory");
			auto memory_before_settings = get_memory_settings(memory_before);
			for(auto & element: memory_before_settings)
				b->write_unibus_byte(element.first, element.second);
		}

		int run_n_instructions = json_integer_value(json_object_get(before, "run-n-instructions"));
		int cur_n_errors       = 0;

		// DO!
		for(int k=0; k<run_n_instructions; k++) {
			DOLOG(log_ss::LS_TRACE, "instruction %d out of %d", k + 1, run_n_instructions);

			auto rc = disassemble(c, nullptr, c->getPC(), false);
			DOLOG(log_ss::LS_TRACE, "%s", std::get<3>(rc).c_str());
			if (c->step() == false) {
				cnsl->put_string_lf("Treated as an invalid instruction");
				cur_n_errors++;
				break;
			}
		}
		auto rc2 = disassemble(c, nullptr, c->getPC(), false);
		DOLOG(log_ss::LS_TRACE, "%s (after)", std::get<3>(rc2).c_str());

		// VERIFY
		if (cur_n_errors == 0) {
			json_t *after = json_object_get(element, "after");

			cur_n_errors += !compare_values(cnsl, c->getPC(),  json_integer_value(json_object_get(after, "PC" )), "PC" );
			cur_n_errors += !compare_values(cnsl, c->getPSW(), json_integer_value(json_object_get(after, "PSW")), "PSW");

			for(int i=0; i<4; i++)
				cur_n_errors += !compare_values(cnsl, c->get_stackpointer(i), json_integer_value(json_object_get(after, format("stack-%d", i).c_str())), format("Stack pointer %d", i));

			cur_n_errors += !compare_values(cnsl, b->getMMU()->getMMR1(), json_integer_value(json_object_get(after, "mmr1")), "MMR1");
			cur_n_errors += !compare_values(cnsl, b->getMMU()->getMMR2(), json_integer_value(json_object_get(after, "mmr2")), "MMR2");

			for(int set=0; set<2; set++) {
				for(int i=0; i<6; i++)
					cur_n_errors += !compare_values(cnsl, c->lowlevel_register_get(set, i), json_integer_value(json_object_get(after, format("reg-%d.%d", i, set).c_str())), format("Register %d", i));
			}

			json_t *memory_after = json_object_get(after, "memory");
			auto memory_after_settings = get_memory_settings(memory_after);
			for(auto & element: memory_after_settings)
				cur_n_errors += !compare_values(cnsl, b->read_physical_byte(element.first), element.second, format("Memory address %06o", element.first));
		}

		total_error_count      +=   cur_n_errors;
		tests_with_error_count += !!cur_n_errors;

		const char *id = json_string_value(json_object_get(element, "id"));
		DOLOG(log_ss::LS_TRACE, "Test result for %d, id: %s: %s", n_tests, id, cur_n_errors ? "FAILED":"OK");

		// clean-up
		delete b;
	}

	json_decref(doc);

	cnsl->put_string_lf(format("test count: %d, tests with errors: %d, total error count: %d", n_tests, tests_with_error_count, total_error_count));

	return 0;
}
#endif

void start_disk_devices(const std::vector<disk_backend *> & backends, const bool enable_snapshots)
{
	for(auto & backend: backends) {
		if (backend->begin(enable_snapshots) == false)
			error_exit(false, "Failed to initialize disk backend \"%s\"", backend->get_identifier().c_str());
	}
}

void help()
{
	printf("-h       this help\n");
#if IS_POSIX
	printf("-D x     deserialize state from file\n");
	printf("-P       when serializing state to file (in the debugger), include an overlay: changes to disk-files are then non-persistent, they only exist in the state-dump\n");
#endif
	printf("-T t.bin load file as a binary tape file (like simh \"load\" command), also see -B\n");
	printf("-B x     1: only load tape (default), 2: load & boot tape, 3: run as a unit test (for .BIC files)\n");
	printf("-r d.img load file as a disk device\n");
	printf("-N host:port  use NBD-server as disk device (like -r)\n");
	printf("-R x     select disk type (rk05, rl02, rp06 or rp07)\n");
	printf("-p 123   set CPU start pointer to octal value\n");
	printf("-b x     enable builtin bootloader, see -R for values (+ \"tm11\") of x\n");
	printf("-u x     ncurses, imgui (optional)\n");
	printf("-d       enable debugger\n");
	printf("-f x     first process the commands from file x before entering the debugger\n");
	printf("-S x     set ram size (in number of 8 kB pages)\n");
	printf("-s x,y   set console switche state: set bit x (0...15) to y (0/1)\n");
	printf("-t       enable tracing (disassemble to stderr, requires -d as well)\n");
	printf("-l x     log to file x\n");
	printf("%s\n", ("-L x[,...] select what subsystems to log to a file (" + get_all_available_log_ss_masks() + ")").c_str());
	printf("-C x[,...] select what subsystems to log to the console\n");
	printf("-X       do not include timestamp in logging\n");
#if defined(JANSSON)
	printf("-J x     run validation suite x against the CPU emulation\n");
#endif
	printf("-1 x     use x as device for DZ-11 (instead of 8 tcp-sockets starting at port %d)\n", default_port_offset);
	printf("-2       set DZ-11 tcp-socket sessions to initialize as a telnet session\n");
	printf("-8 x     setup a blinkenlights/PiDP11 connection on IP-address x\n");
	printf("-Q x     use x as port offset instead of %d\n", default_port_offset);
	printf("-I x[,y,z,[a]] setup a DEQNA device with Ethernet type x ('linux' (tap), 'vxlan': y=ip,z=port,a=id)\n");
}

int main(int argc, char *argv[])
{
	//setlocale(LC_ALL, "");

#if defined(_WIN32)
	WSADATA wsa_data { };
	if (WSAStartup(0x202, &wsa_data) != 0)
		printf("ERROR: winsock initialization failure\n");
#endif

	std::vector<disk_backend *> disk_files;
	std::string  disk_type = "rk05";

	enum { ui_ncurses, ui_imgui, ui_none } with_ui = ui_none;
	bool          run_debugger  = false;
	std::optional<std::string> debugger_init;

	bootloader_t  bootloader    = BL_NONE;

	const char  *logfile   = nullptr;
	bool         timestamp = true;
	std::optional<std::string> log_subsystems_file;
	std::optional<std::string> log_subsystems_console;

	std::optional<uint16_t> start_addr;

	std::string  tape;
	enum { just_load, load_and_run, load_and_run_bic } tape_mode = just_load;

	uint16_t     console_switches = 0;
	std::string  blinkenlights_ip;
	std::string  ddp_ip;
	std::optional<int> console_port;

	bool         disk_snapshots = false;

	std::optional<int> set_ram_size;

	std::string  validate_json;

	std::string  deserialize;

	std::optional<std::string> dz11_device;
	bool         dz11_setup_telnet = false;
	bool         dc11_setup_telnet = false;

	int          tcp_port_offset = default_port_offset;

	std::string  deqna_type;

	int  opt = -1;
	while((opt = getopt(argc, argv, "u:hC:L:D:T:B:r:R:p:df:tb:l:s:Q:N:J:XS:P1:m:Q:28:9:I:c:")) != -1)
	{
		switch(opt) {
			case 'h':
				help();
				return 1;

			case 'Q':
				tcp_port_offset = atoi(optarg);
				break;

			case 'f':
				debugger_init = optarg;
				break;

			case '1':
				dz11_device = optarg;
				break;

			case '2':
				dz11_setup_telnet = true;
				dc11_setup_telnet = true;
				break;

			case 'D':
				deserialize = optarg;
				break;

			case 'X':
				timestamp = false;
				break;

#if defined(JANSSON)
			case 'J':
				validate_json = optarg;
				break;
#endif

			case 's': {
					char *c = strchr(optarg, ',');
					if (!c)
						error_exit(false, "-s: parameter missing");
					int bit   = atoi(optarg);
					int state = atoi(c + 1);

					console_switches &= ~(1 << bit);
					console_switches |= state << bit;

					break;
				  }

			case 'b':
				  if (strcmp(optarg, "rk05") == 0)
					  bootloader = BL_RK05;
				  else if (strcmp(optarg, "rl02") == 0)
					  bootloader = BL_RL02;
				  else if (strcmp(optarg, "rp06") == 0 || strcmp(optarg, "rp07") == 0)
					  bootloader = BL_RP06;
				  else if (strcmp(optarg, "tm11") == 0)
					  bootloader = BL_TM11;
				  else 
					  error_exit(false, "Internal error: bootloader type %s not understood", optarg);
				break;

			case 'd':
				run_debugger = true;
				break;

			case 't':
				set_ss_log(true,  log_ss::LS_TRACE);
				set_ss_log(false, log_ss::LS_TRACE);
				break;

			case 'u':
				if (strcmp(optarg, "ncurses") == 0)
					with_ui = ui_ncurses;
				else if (strcmp(optarg, "imgui") == 0)
					with_ui = ui_imgui;
				else if (strcmp(optarg, "none") == 0)
					with_ui = ui_none;
				else
					error_exit(false, "\"%s\" is not known for -u", optarg);
				break;

			case 'T':
				tape = optarg;
				break;

			case 'B': {
					  int temp = std::stoi(optarg);
					  if (temp == 1)
						  tape_mode = just_load;
					  else if (temp == 2)
						  tape_mode = load_and_run;
					  else if (temp == 3)
						  tape_mode = load_and_run_bic;
					  else
						error_exit(false, "Tape usage mode");
				}
				break;

			case 'R':
				disk_type = optarg;
				if (disk_type != "rk05" && disk_type != "rl02" && disk_type != "rp06" && disk_type != "rp07")
					error_exit(false, "Disk type not known");
				break;

			case 'r':
				disk_files.push_back(new disk_backend_file(optarg));
				break;

			case 'N': {
					  auto parts = split(optarg, ":");
					  if (parts.size() != 2)
						  error_exit(false, "-N: parameter missing");

					  disk_files.push_back(new disk_backend_nbd(parts.at(0), std::stoi(parts.at(1))));
				  }
				  break;

			case 'p':
				start_addr = std::stoi(optarg, nullptr, 8);
				break;

			case 'l':
				logfile = optarg;
				break;

			case 'L':
				log_subsystems_file = optarg;
				break;

			case 'C':
				log_subsystems_console = optarg;
				break;

			case 'S':
				set_ram_size = std::stoi(optarg);
				break;

			case 'P':
				disk_snapshots = true;
				break;

			case '8':
				blinkenlights_ip = optarg;
				break;

			case '9':
				ddp_ip = optarg;
				break;

			case 'I':
				deqna_type = optarg;
				break;

			case 'c':
				console_port = std::stoi(optarg);
				break;

			default:
			        fprintf(stderr, "-%c is not understood\n", opt);
				return 1;
		}
	}

	console *cnsl = nullptr;

	setlogfile(logfile, timestamp);
	if (logfile == nullptr)
		disable_all_log_ss(false);

	if (log_subsystems_file.has_value()) {
		disable_all_log_ss(false);
		auto parts = split(log_subsystems_file.value(), ",");
		for(auto & ss: parts) {
			if (toggle_ss_log(false, ss) == false)
				error_exit(false, "\"%s\" is now known", ss.c_str());
		}
	}

	if (log_subsystems_console.has_value()) {
		disable_all_log_ss(true);
		auto parts = split(log_subsystems_console.value(), ",");
		for(auto & ss: parts) {
			if (toggle_ss_log(true, ss) == false)
				error_exit(false, "\"%s\" is now known", ss.c_str());
		}
	}

	DOLOG(log_ss::LS_GENERIC, "PDP11 emulator, by Folkert van Heusden");
	DOLOG(log_ss::LS_GENERIC, "Built on: " __DATE__ " " __TIME__);

	start_disk_devices(disk_files, disk_snapshots);

	std::thread *panel_th = nullptr;
	if (console_port.has_value()) {
		auto io = new comm_tcp_socket_server(console_port.value(), true);
		cnsl = new console_comm(&event, io, 80, 24);
		if (io->begin() == false)
			error_exit(false, "Failed setting up TCP listener on port %d", console_port.value());
	}
	else {
#if defined(_WIN32)
		cnsl = new console_posix(&event);
#else
		if (with_ui == ui_ncurses) {
			cnsl = new console_ncurses(&event);
			set_terminal(cnsl);
		}
#if defined(USE_IMGUI)
		else if (with_ui == ui_imgui) {
			cnsl = new console_imgui(&event);
			set_terminal(cnsl);
			panel_th = new std::thread([&] { cnsl->panel_update_thread(); });
		}
#endif
		else {
			cnsl = new console_posix(&event);
		}
#endif
	}

#if !defined(_WIN32) && defined(JANSSON)
	if (validate_json.empty() == false) {
		int rc = run_cpu_validation(cnsl, validate_json);
		delete cnsl;
		return rc;
	}
#endif

	bus *b = nullptr;

	if (deserialize.empty()) {
		b = new bus();

		if (set_ram_size.has_value())
			b->set_memory_size(set_ram_size.value());
		else
			b->set_memory_size(DEFAULT_N_PAGES);

		b->set_console_switches(console_switches);

		cpu *c = new cpu(b, &event);
		b->add_cpu(c);

		auto rk05_dev = new rk05(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
		rk05_dev->begin();
		b->add_rk05(rk05_dev);

		auto rl02_dev = new rl02(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
		rl02_dev->begin();
		b->add_rl02(rl02_dev);

		auto rp06_dev = new rp06(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag(), disk_type == "rp07");
		rp06_dev->begin();
		b->add_RP06(rp06_dev);

		if (deqna_type.empty() == false) {
			auto parts = split(deqna_type, ",");
			eth_transport *et = nullptr;

			if (false) {
			}
#if defined(linux)
			else if (parts[0] == "linux")
				et = new eth_transport_linux("pdp");
#endif
			else if (parts[0] == "vxlan") {
				if (parts.size() == 3)
					et = new eth_transport_vxlan(parts[1], std::stoi(parts[2]));
				else if (parts.size() == 4)
					et = new eth_transport_vxlan(parts[1], std::stoi(parts[2]), std::stoi(parts[3]));
				else
					error_exit(false, "vxlan: incorrect number of parameters");
			}
			else {
				error_exit(false, "Link layer \"%s\" is not known", parts[0].c_str());
			}

			if (!et->begin())
				error_exit(false, "Failed to initialize link layer for DEQNA");

			uint8_t mac_address[8] { };
			get_deqna_mac(mac_address);
			auto deqna_dev = new deqna(b, mac_address, et, cnsl->get_network_activity_flag());
			if (deqna_dev->begin() == false)
				error_exit(false, "Failed to setup DEQNA device");
			b->add_DEQNA(deqna_dev);
			DOLOG(log_ss::LS_GENERIC, "DEQNA initialized");
		}

		if (disk_type == "rk05") {
			for(auto & file: disk_files)
				rk05_dev->access_disk_backends()->push_back(file);
		}
		else if (disk_type == "rl02") {
			for(auto & file: disk_files)
				rl02_dev->access_disk_backends()->push_back(file);
		}
		else if (disk_type == "rp06" || disk_type == "rp07") {
			for(auto & file: disk_files)
				rp06_dev->access_disk_backends()->push_back(file);
		}
		else {
			error_exit(false, "Internal error: disk-type %s not understood", disk_type.c_str());
		}
	}
#if IS_POSIX
	else {
		auto rc = deserialize_file(deserialize);
		if (rc.has_value() == false)
			error_exit(true, "Failed to open %s", deserialize.c_str());

		b = bus::deserialize(rc.value(), cnsl, &event);

		myusleep(251000);  // ??? TODO
	}
#endif

	if (b->getTty() == nullptr) {
		tty *tty_ = new tty(cnsl, b);
		b->add_tty(tty_);
	}

	cnsl->set_bus(b);
	cnsl->begin();

	if (bl->begin()) {
		cnsl->set_blinkenlights_panel(bl);
		if (blinkenlights_ip.empty() == false)
			bl->set_target(blinkenlights_ip);
	}
	else {
		DOLOG(log_ss::LS_GENERIC, "Cannot initialize blinkenlights");
	}

	if (ddp_->begin()) {
		cnsl->set_ddp_panel(ddp_);
		if (ddp_ip.empty() == false)
			ddp_->set_target(ddp_ip);
	}
	else {
		DOLOG(log_ss::LS_GENERIC, "Cannot initialize ddp");
	}

	//// DZ11
	comm_io *io_channels = new comm_io(dz11_n_lines);
	constexpr const int bitrate = 38400;

#if !defined(_WIN32)
	if (dz11_device.has_value()) {
		DOLOG(log_ss::LS_GENERIC, "Configuring DZ11 device for TTY on %s (%d bps)", dz11_device.value().c_str(), bitrate);
		if (io_channels->set_device(0, new comm_posix_tty(dz11_device.value(), bitrate)) == false)
			DOLOG(log_ss::LS_GENERIC, "Failed to configure device");
	}
#endif

	for(size_t i=0; i<dz11_n_lines; i++) {
		if (io_channels->is_defined(i))
			continue;
		int port = tcp_port_offset + i;
		DOLOG(log_ss::LS_GENERIC, "Configuring DZ11 device for TCP socket on port %d", port);
		if (io_channels->set_device(i, new comm_tcp_socket_server(port, dz11_setup_telnet)) == false)
			DOLOG(log_ss::LS_GENERIC, "Failed to configure device");
	}

	dz11 *dz11_ = new dz11(b, io_channels);
	dz11_->begin();
	b->add_DZ11(dz11_);
	//
	//// DC11
	comm_io *io_channels2 = new comm_io(dc11_n_lines);
	for(size_t i=0; i<dc11_n_lines; i++) {
		if (io_channels2->is_defined(i))
			continue;
		int port = tcp_port_offset + i + dz11_n_lines;
		DOLOG(log_ss::LS_GENERIC, "Configuring DC11 device for TCP socket on port %d", port);
		if (io_channels2->set_device(i, new comm_tcp_socket_server(port, dc11_setup_telnet)) == false)
			DOLOG(log_ss::LS_GENERIC, "Failed to configure device");
	}
	dc11 *dc11_ = new dc11(b, io_channels2);
	dc11_->begin();
	b->add_DC11(dc11_);
	//

	tm_11 *tm_11_ = new tm_11(b);
	b->add_tm11(tm_11_);
	//

	running = cnsl->get_running_flag();

	std::atomic_bool interrupt_emulation { false };

	if (bootloader != BL_NONE) {
		auto bl_addr = set_boot_loader(b, bootloader);
		if (start_addr.has_value() == false)
			start_addr = bl_addr;
	}

	if (tape.empty() == false) {
		if (tape_mode == load_and_run_bic) {
			auto tape_start = load_tape(b, tape, cnsl);
			if (tape_start.has_value())
				start_addr = tape_start;
			else
				start_addr = 0200;
		}
		else if (tape_mode == load_and_run || tape_mode == just_load) {
			tm_11_->load(tape);
		}
	}

	if (start_addr.has_value())
		b->getCpu()->set_register(7, start_addr.value());

	DOLOG(log_ss::LS_GENERIC, "Start running at %06o", b->getCpu()->get_register(7));

#if defined(_WIN32)
	signal(SIGINT, sw_handler);
#else
	struct sigaction sa { };
	sa.sa_handler = sw_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	if (with_ui == ui_ncurses)
		sigaction(SIGWINCH, &sa, nullptr);

	sigaction(SIGTERM, &sa, nullptr);
	sigaction(SIGINT , &sa, nullptr);
#endif

	cnsl->start_thread();

	b->getKW11_L()->begin(cnsl);

	if (tape_mode == load_and_run_bic || tape_mode == load_and_run)
		simple_run(cnsl, b, &event);
	else if (run_debugger || (bootloader == BL_NONE && tape.empty()))
		debugger(cnsl, b, &event, debugger_init);
	else {
		for(;;) {
			*running = true;

			while(event == EVENT_NONE)
				b->getCpu()->step();

			*running = false;

			uint32_t stop_event = event.exchange(EVENT_NONE);
			if (stop_event == EVENT_HALT || stop_event == EVENT_INTERRUPT || stop_event == EVENT_TERMINATE)
				break;
		}
	}

	event = EVENT_TERMINATE;

	cnsl->stop_thread();
	if (panel_th) {
		panel_th->join();
		delete panel_th;
	}

	delete b;
	delete cnsl;

	return 0;
}
