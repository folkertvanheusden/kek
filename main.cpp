// (C) 2018-2025 by Folkert van Heusden
// Released under MIT license

#include <ArduinoJson.h>
#include <assert.h>
#include <atomic>
#include <cinttypes>
#include <signal.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <unistd.h>

#include "error.h"
#include "comm.h"
#include "comm_posix_tty.h"
#include "comm_tcp_socket_server.h"
#if !defined(_WIN32)
#include "console_ncurses.h"
#endif
#include "console_posix.h"
#include "cpu.h"
#include "debugger.h"
#include "disk_backend.h"
#include "disk_backend_file.h"
#include "disk_backend_nbd.h"
#include "dc11.h"
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


bool              withUI       { false };
std::atomic_uint32_t event     { 0 };
std::atomic_bool *running      { nullptr };

std::atomic_bool  sigw_event   { false };

constexpr const uint16_t validation_psw_mask = 0174037;  // ignore unused bits & priority(!)

#if !defined(_WIN32)
void sw_handler(int s)
{
	if (s == SIGWINCH)
		sigw_event = true;
	else {
		fprintf(stderr, "Terminating...\n");

		event = EVENT_TERMINATE;
	}
}

std::vector<std::pair<uint32_t, uint8_t> > get_memory_settings(const JsonArrayConst & ja)
{
	std::vector<std::pair<uint32_t, uint8_t> > out;
	for(auto kv_dict: ja) {
		// should be one element
		for(const auto & kv: kv_dict.as<JsonObjectConst>()) {
			uint32_t a = std::stoi(kv.key().c_str(), nullptr, 8);
			uint16_t v = kv.value().as<int>();
			out.push_back({ a, v & 255 });
			out.push_back({ a + 1, v >> 8 });
		}
	}
	return out;
}

uint16_t get_register_value(const JsonObjectConst & o, const std::string & name)
{
	return o[name].as<int>();
}

bool compare_values(console *const cnsl, uint32_t v, uint32_t should_be, const std::string & name)
{
	if (v == should_be)
		return true;

	cnsl->put_string_lf(format("%s: %o (is) != %o (should be)", name.c_str(), v, should_be));

	return false;
}

int run_cpu_validation(console *const cnsl, const std::string & filename)
{
	std::optional<JsonDocument> doc = deserialize_file(filename);
	if (doc.has_value() == false)
		return -1;

	int n_tests               = 0;
	int total_error_count      = 0;
	int tests_with_error_count = 0;

	JsonArray array = doc.value().as<JsonArray>();
	for(JsonObjectConst test : array) {
		n_tests++;

		cnsl->put_string_lf(format("Test %d: %s", n_tests, test["id"].as<std::string>().c_str()));

		// create environment
		event = 0;
		bus *b = new bus();
		b->set_memory_size(DEFAULT_N_PAGES * 8192l);
		cpu *c = new cpu(b, &event);
		b->add_cpu(c);

		// SET
		{
			auto before = test["before"];

			// PC
			c->setPC(get_register_value(before, "PC"));

			// PSW
			c->setPSW(get_register_value(before, "PSW"), false);

			// stackpointers
			for(int i=0; i<4; i++)
				c->set_stackpointer(i, get_register_value(before, format("stack-%d", i)));

			// registers
			int set = 0;
			for(int i=0; i<6; i++)
				c->set_register(i, get_register_value(before, format("reg-%d.%d", i, set)));

			// memory
			auto memory_before = before["memory"];
			auto memory_before_settings = get_memory_settings(memory_before);
			for(auto & element: memory_before_settings)
				b->write_physical(element.first, element.second);
		}

		// DO!
		c->emulation_start();
		disassemble(c, cnsl, c->getPC(), false);
		c->step();

		// VERIFY
		int cur_n_errors = 0;
		{
			auto after = test["after"];

			cur_n_errors += !compare_values(cnsl, c->getPC(),  get_register_value(after, "PC" ), "PC" );
			cur_n_errors += !compare_values(cnsl, c->getPSW(), get_register_value(after, "PSW"), "PSW");

			for(int i=0; i<4; i++)
				cur_n_errors += !compare_values(cnsl, c->get_stackpointer(i), get_register_value(after, format("stack-%d", i)), format("Stack pointer %d", i));

			int set = 0;
			for(int i=0; i<6; i++)
				cur_n_errors += !compare_values(cnsl, c->get_register(i), get_register_value(after, format("reg-%d.%d", i, set)), format("Register %d", i));

			auto memory_after = after["memory"];
			auto memory_after_settings = get_memory_settings(memory_after);
			for(auto & element: memory_after_settings)
				cur_n_errors += !compare_values(cnsl, b->read_physical_byte(element.first), element.second, format("Memory address %06o", element.first));
		}

		total_error_count      +=   cur_n_errors;
		tests_with_error_count += !!cur_n_errors;
		

		// clean-up
		delete b;
	}

	cnsl->put_string_lf(format("test count: %d, tests with errors: %d, total error count: %d", n_tests, tests_with_error_count, total_error_count));

	return 0;
}
#endif

void get_metrics(cpu *const c)
{
	set_thread_name("kek:metrics");

	uint64_t previous_instruction_count = c->get_instructions_executed_count();
	uint64_t previous_ts                = get_us();
	uint64_t previous_idle_time         = c->get_wait_time();

	while(event != EVENT_TERMINATE) {
		sleep(1);

		uint64_t ts        = get_us();
		uint64_t idle_time = c->get_wait_time();
		uint64_t current_instruction_count = c->get_instructions_executed_count();

		uint64_t current_idle_duration = idle_time - previous_idle_time;

		auto stats = c->get_mips_rel_speed(current_instruction_count - previous_instruction_count, ts - previous_ts - current_idle_duration);

		FILE *fh = fopen("kek-metrics.csv", "a+");
		if (fh) {
			fseek(fh, 0, SEEK_END);
			if (ftell(fh) == 0)
				fprintf(fh, "timestamp,MIPS,relative speed in %%,instructions executed count,idle time\n");
			fprintf(fh, "%.06f, %.2f, %.2f%%, %" PRIu64 ", %.3f\n", ts / 1000., std::get<0>(stats), std::get<1>(stats), std::get<2>(stats), current_idle_duration / 1000000.);
			fclose(fh);
		}

		previous_idle_time         = idle_time;
		previous_instruction_count = current_instruction_count;
		previous_ts                = ts;
	}
}

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
	printf("-D x     deserialize state from file\n");
	printf("-P       when serializing state to file (in the debugger), include an overlay: changes to disk-files are then non-persistent, they only exist in the state-dump\n");
	printf("-T t.bin load file as a binary tape file (like simh \"load\" command), also for .BIC files\n");
	printf("-B       run tape file as a unit test (for .BIC files)\n");
	printf("-r d.img load file as a disk device\n");
	printf("-N host:port  use NBD-server as disk device (like -r)\n");
	printf("-R x     select disk type (rk05, rl02 or rp06)\n");
	printf("-p 123   set CPU start pointer to decimal(!) value\n");
	printf("-b       enable bootloader (builtin)\n");
	printf("-n       ncurses UI\n");
	printf("-d       enable debugger\n");
	printf("-S x     set ram size (in number of 8 kB pages)\n");
	printf("-s x,y   set console switche state: set bit x (0...15) to y (0/1)\n");
	printf("-t       enable tracing (disassemble to stderr, requires -d as well)\n");
	printf("-l x     log to file x\n");
	printf("-L x,y   set log level for screen (x) and file (y)\n");
	printf("-X       do not include timestamp in logging\n");
	printf("-J x     run validation suite x against the CPU emulation\n");
	printf("-M       log metrics\n");
	printf("-1 x     use x as device for DC-11\n");
}

int main(int argc, char *argv[])
{
	//setlocale(LC_ALL, "");

	std::vector<disk_backend *> disk_files;
	std::string  disk_type = "rk05";

	bool run_debugger = false;

	bool          enable_bootloader = false;
	bootloader_t  bootloader        = BL_NONE;

	const char  *logfile   = nullptr;
	log_level_t  ll_screen = none;
	log_level_t  ll_file   = none;
	bool         timestamp = true;

	uint16_t     start_addr= 01000;
	bool         sa_set    = false;

	std::string  tape;
	bool         is_bic    = false;

	uint16_t     console_switches = 0;

	std::string  test;

	bool         disk_snapshots = false;

	std::optional<int> set_ram_size;

	std::string  validate_json;

	bool         metrics = false;

	std::string  deserialize;

	bool         benchmark = false;

	std::optional<std::string> dc11_device;

	int  opt          = -1;
	while((opt = getopt(argc, argv, "hqD:MT:Br:R:p:ndtL:bl:s:Q:N:J:XS:P1:")) != -1)
	{
		switch(opt) {
			case 'h':
				help();
				return 1;

			case '1':
				dc11_device = optarg;
				break;

			case 'D':
				deserialize = optarg;
				break;

			case 'M':
				metrics = true;
				break;

			case 'X':
				timestamp = false;
				break;

			case 'J':
				validate_json = optarg;
				break;

			case 'Q':
				test = optarg;
				break;

			case 'q':
				benchmark = true;
				break;

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
				enable_bootloader = true;
				break;

			case 'd':
				run_debugger = true;
				break;

			case 't':
				settrace(true);
				break;

			case 'n':
				withUI = true;
				break;

			case 'T':
				tape = optarg;
				break;

			case 'B':
				is_bic = true;
				break;

			case 'R':
				disk_type = optarg;
				if (disk_type != "rk05" && disk_type != "rl02" && disk_type != "rp06")
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
				start_addr = atoi(optarg);
				sa_set     = true;
				break;

			case 'L': {
					auto parts = split(optarg, ",");

					if (parts.size() != 2)
						error_exit(false, "Argument missing for -L");

					ll_screen  = parse_ll(parts[0]);
					ll_file    = parse_ll(parts[1]);
				  }
				break;

			case 'l':
				logfile = optarg;
				break;

			case 'S':
				set_ram_size = std::stoi(optarg);
				break;

			case 'P':
				disk_snapshots = true;
				break;

			default:
			        fprintf(stderr, "-%c is not understood\n", opt);
				return 1;
		}
	}

	console *cnsl = nullptr;

	setlogfile(logfile, ll_file, ll_screen, timestamp);

	DOLOG(info, true, "PDP11 emulator, by Folkert van Heusden");

	DOLOG(info, true, "Built on: " __DATE__ " " __TIME__);

	start_disk_devices(disk_files, disk_snapshots);

#if defined(_WIN32)
	cnsl = new console_posix(&event);
#else
	if (withUI) {
		cnsl = new console_ncurses(&event);
		set_terminal(cnsl);
	}
	else {
		cnsl = new console_posix(&event);
	}
#endif

#if !defined(_WIN32)
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
			b->set_memory_size(DEFAULT_N_PAGES * 8192l);

		b->set_console_switches(console_switches);

		cpu *c = new cpu(b, &event);
		b->add_cpu(c);

		auto rk05_dev = new rk05(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
		rk05_dev->begin();
		b->add_rk05(rk05_dev);

		auto rl02_dev = new rl02(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
		rl02_dev->begin();
		b->add_rl02(rl02_dev);

		auto rp06_dev = new rp06(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
		rp06_dev->begin();
		b->add_RP06(rp06_dev);

		if (disk_type == "rk05") {
			bootloader = BL_RK05;

			for(auto & file: disk_files)
				rk05_dev->access_disk_backends()->push_back(file);
		}
		else if (disk_type == "rl02") {
			bootloader = BL_RL02;

			for(auto & file: disk_files)
				rl02_dev->access_disk_backends()->push_back(file);
		}
		else if (disk_type == "rp06") {
			bootloader = BL_RP06;

			for(auto & file: disk_files)
				rp06_dev->access_disk_backends()->push_back(file);
		}
		else {
			error_exit(false, "Internal error: disk-type %s not understood", disk_type.c_str());
		}

		if (enable_bootloader)
			set_boot_loader(b, bootloader);
	}
	else {
		auto rc = deserialize_file(deserialize);
		if (rc.has_value() == false)
			error_exit(true, "Failed to open %s", deserialize.c_str());

		b = bus::deserialize(rc.value(), cnsl, &event);

		myusleep(251000);
	}

	if (b->getTty() == nullptr) {
		tty *tty_ = new tty(cnsl, b);

		b->add_tty(tty_);
	}

	cnsl->set_bus(b);
	cnsl->begin();

	//// DC11
	constexpr const int bitrate = 38400;

	std::vector<comm *> comm_interfaces;
	if (dc11_device.has_value()) {
		DOLOG(info, false, "Configuring DC11 device for TTY on %s (%d bps)", dc11_device.value().c_str(), bitrate);
		comm_interfaces.push_back(new comm_posix_tty(dc11_device.value(), bitrate));
	}

	for(size_t i=comm_interfaces.size(); i<4; i++) {
		int port = 1100 + i;
		comm_interfaces.push_back(new comm_tcp_socket_server(port));
		DOLOG(info, false, "Configuring DC11 device for TCP socket on port %d", port);
	}

	for(auto & c: comm_interfaces) {
		if (c->begin() == false)
			DOLOG(warning, false, "Failed to configure %s", c->get_identifier().c_str());
	}

	dc11 *dc11_ = new dc11(b, comm_interfaces);
	dc11_->begin();
	b->add_DC11(dc11_);
	//

	tm_11 *tm_11_ = new tm_11(b);
	b->add_tm11(tm_11_);

	running = cnsl->get_running_flag();

	std::atomic_bool interrupt_emulation { false };

	std::optional<uint16_t> bic_start;

	if (tape.empty() == false) {
		bic_start = load_tape(b, tape);

		if (bic_start.has_value() == false)
			return 1;  // fail

		b->getCpu()->set_register(7, bic_start.value());
	}

	if (sa_set)
		b->getCpu()->set_register(7, start_addr);

	DOLOG(info, true, "Start running at %06o", b->getCpu()->get_register(7));

#if !defined(_WIN32)
	struct sigaction sa { };
	sa.sa_handler = sw_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	if (withUI)
		sigaction(SIGWINCH, &sa, nullptr);

	sigaction(SIGTERM, &sa, nullptr);
	sigaction(SIGINT , &sa, nullptr);
#endif

	if (test.empty() == false)
		load_p11_x11(b, test);

	std::thread *metrics_thread = nullptr;
	if (metrics)
		metrics_thread = new std::thread(get_metrics, b->getCpu());

	cnsl->start_thread();

	b->getKW11_L()->begin(cnsl);

	if (is_bic)
		run_bic(cnsl, b, &event, bic_start.value());
	else if (run_debugger || (bootloader == BL_NONE && test.empty() && tape.empty()))
		debugger(cnsl, b, &event);
	else if (benchmark) {
		// FILL MEMORY
		memory *m = b->getRAM();
		for(uint32_t i=0; i<m->get_memory_size(); i++)
			m->write_byte(i, i * 7);
		// SET MMU TO ENABLED
		b->getMMU()->setMMR0_as_is(1);  // enable MMU
		// run for a second
		b->getCpu()->setPC(0);
		b->getCpu()->emulation_start();  // for statistics
		uint64_t start = get_us();
		do {
			// disassemble(b->getCpu(), nullptr, b->getCpu()->getPC(), false);
			uint16_t before_pc = b->getCpu()->getPC();
			b->getCpu()->step();
			if (b->getCpu()->getPC() == before_pc)
				b->getCpu()->setPC(before_pc + 4);
		}
		while(get_us() - start <= 5000000);

		auto stats = b->getCpu()->get_mips_rel_speed({ }, { });
		cnsl->put_string_lf(format("MIPS: %.2f, relative speed: %.2f%%, instructions executed: %" PRIu64 " in %.2f seconds", std::get<0>(stats), std::get<1>(stats), std::get<2>(stats), std::get<3>(stats) / 1000000.));
	}
	else {
		b->getCpu()->emulation_start();  // for statistics

		for(;;) {
			*running = true;

			while(event == EVENT_NONE)
				b->getCpu()->step();

			*running = false;

			uint32_t stop_event = event.exchange(EVENT_NONE);
			if (stop_event == EVENT_HALT || stop_event == EVENT_INTERRUPT || stop_event == EVENT_TERMINATE)
				break;
		}

		auto stats = b->getCpu()->get_mips_rel_speed({ }, { });
		cnsl->put_string_lf(format("MIPS: %.2f, relative speed: %.2f%%, instructions executed: %" PRIu64 " in %.2f seconds", std::get<0>(stats), std::get<1>(stats), std::get<2>(stats), std::get<3>(stats) / 1000000.));
	}

	event = EVENT_TERMINATE;

	if (metrics_thread) {
		metrics_thread->join();
		delete metrics_thread;
	}

	cnsl->stop_thread();

	delete b;

	delete cnsl;

	return 0;
}
