// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

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

int run_cpu_validation(const std::string & filename)
{
#if 0
	json_error_t error;
	json_t *json = json_load_file(filename.c_str(), JSON_REJECT_DUPLICATES, &error);
	if (!json)
		error_exit(false, "%s", error.text);

	size_t n_ok = 0;
	size_t array_size = json_array_size(json);
	for(size_t i=0; i<array_size; i++) {
		json_t *test = json_array_get(json, i);

		// create environment
		event = 0;
		bus *b = new bus();
		b->set_memory_size(DEFAULT_N_PAGES * 8192l);
		cpu *c = new cpu(b, &event);
		b->add_cpu(c);

		uint16_t start_pc = 0;

		{
			// initialize
			json_t *memory_before = json_object_get(test, "memory-before");
			const char *key   = nullptr;
			json_t     *value = nullptr;
			json_object_foreach(memory_before, key, value) {
				b->writePhysical(atoi(key), json_integer_value(value));
			}
		}

		json_t *const registers_before = json_object_get(test, "registers-before");

		{
			const char *key   = nullptr;
			json_t     *value = nullptr;
			json_t *set0 = json_object_get(registers_before, "0");
			json_object_foreach(set0, key, value) {
				c->lowlevel_register_set(0, atoi(key), json_integer_value(value));
			}
			json_t *set1 = json_object_get(registers_before, "1");
			json_object_foreach(set1, key, value) {
				c->lowlevel_register_set(1, atoi(key), json_integer_value(value));
			}
		}
		{
			json_t *psw_reg = json_object_get(registers_before, "psw");
			assert(psw_reg);
			c->lowlevel_psw_set(json_integer_value(psw_reg));
		}
		{
			json_t *b_pc = json_object_get(registers_before, "pc");
			assert(b_pc);
			start_pc = json_integer_value(b_pc);
			c->setPC(start_pc);
		}
		{
			json_t *b_sp = json_object_get(registers_before, "sp");
			size_t sp_array_size = json_array_size(b_sp);
			assert(sp_array_size == 4);
			for(size_t i=0; i<sp_array_size; i++) {
				json_t *temp = json_array_get(b_sp, i);
				c->lowlevel_register_sp_set(i, json_integer_value(temp));
			}
		}

		{
			json_t *a_mmr0 = json_object_get(test, "mmr0-before");
			assert(a_mmr0);
			b->getMMU()->setMMR0(json_integer_value(a_mmr0));
		}

		disassemble(c, nullptr, start_pc, false);
		auto disas_data = c->disassemble(start_pc);
		c->step();

		uint16_t new_pc = c->getPC();

		// validate
		{
			bool err = false;
			{
				json_t *memory_after = json_object_get(test, "memory-after");
				const char *key   = nullptr;
				json_t     *value = nullptr;
				json_object_foreach(memory_after, key, value) {
					int      key_v        = atoi(key);
					uint16_t mem_contains = b->readPhysical(key_v);
					uint16_t should_be    = json_integer_value(value);

					if (mem_contains != should_be) {
						DOLOG(warning, true, "memory address %06o (%d) mismatch (is: %06o (%d), should be: %06o (%d))", key_v, key_v, mem_contains, mem_contains, should_be, should_be);
						err = true;
					}
				}
			}

			uint16_t psw = c->getPSW();

			json_t *const registers_after = json_object_get(test, "registers-after");
			{
				int set_nr = (psw >> 11) & 1;
				char set[] = { char('0' + set_nr), 0x00 };

				json_t *a_set = json_object_get(registers_after, set);
				const char *key   = nullptr;
				json_t     *value = nullptr;
				json_object_foreach(a_set, key, value) {
					uint16_t register_is = c->lowlevel_register_get(set_nr, atoi(key));
					uint16_t should_be   = json_integer_value(value);

					if (register_is != should_be) {
						DOLOG(warning, true, "set %d register %s mismatch (is: %06o (%d), should be: %06o (%d))", set_nr, key, register_is, register_is, should_be, should_be);
						err = true;
					}
				}
			}

			{
				json_t *a_pc = json_object_get(registers_after, "pc");
				assert(a_pc);
				uint16_t should_be_pc = json_integer_value(a_pc);
				if (new_pc != should_be_pc) {
					DOLOG(warning, true, "PC register mismatch (is: %06o (%d), should be: %06o (%d))", new_pc, new_pc, should_be_pc, should_be_pc);
					err = true;
				}
			}

			{
				json_t *a_sp = json_object_get(registers_after, "sp");
				size_t sp_array_size = json_array_size(a_sp);
				assert(sp_array_size == 4);
				for(size_t i=0; i<sp_array_size; i++) {
					json_t *temp = json_array_get(a_sp, i);
					uint16_t sp = c->lowlevel_register_sp_get(i);
					if (json_integer_value(temp) != sp) {
						DOLOG(warning, true, "SP[%d] register mismatch (is: %06o (%d), should be: %06o (%d)) for %06o", i, sp, sp, json_integer_value(temp), json_integer_value(temp), b->readPhysical(start_pc));
						err = true;
					}
				}
			}

			{
				json_t *a_psw = json_object_get(registers_after, "psw");
				assert(a_psw);
				uint16_t should_be_psw = json_integer_value(a_psw);
				if ((should_be_psw & validation_psw_mask) != (psw & validation_psw_mask)) {
					DOLOG(warning, true, "PSW register mismatch (is: %06o (%d), w/m %06o, should be: %06o (%d), w/m %06o)", psw, psw, psw & validation_psw_mask, should_be_psw, should_be_psw, should_be_psw & validation_psw_mask);
					err = true;
				}
			}

			for(int r=0; r<4; r++) {
				json_t *a_mmr = json_object_get(test, format("mmr%d-after", r).c_str());
				assert(a_mmr);
				uint16_t should_be_mmr = json_integer_value(a_mmr);
				uint16_t is_mmr = b->getMMU()->getMMR(r);
				if (should_be_mmr != is_mmr) {
					int is_d1 = is_mmr >> 11;
					if (is_d1 & 16)
						is_d1 = -(32 - is_d1);
					int is_r1 = (is_mmr >> 8) & 7;
					int is_d2 = (is_mmr >> 3) & 31;
					if (is_d2 & 16)
						is_d2 = -(32 - is_d2);
					int is_r2 = is_mmr & 7;

					int sb_d1 = should_be_mmr >> 11;
					if (sb_d1 & 16)
						sb_d1 = -(32 - sb_d1);
					int sb_r1 = (should_be_mmr >> 8) & 7;
					int sb_d2 = (should_be_mmr >> 3) & 31;
					if (sb_d2 & 16)
						sb_d2 = -(32 - sb_d2);
					int sb_r2 = should_be_mmr & 7;
					DOLOG(warning, true, "MMR%d register mismatch (is: %06o (%d / %02d,%02d - %02d,%02d), should be: %06o (%d / %02d,%02d - %02d,%02d))%s %s", r, is_mmr, is_mmr, is_d1, is_r1, is_d2, is_r2, should_be_mmr, should_be_mmr, sb_d1, sb_r1, sb_d2, sb_r2, c->is_it_a_trap() ? " TRAP": "", disas_data["instruction-text"].at(0).c_str());
					err = true;
				}
			}

			if (err) {
				if (c->is_it_a_trap())
					DOLOG(warning, true, "Error by TRAP %s", disas_data["instruction-text"].at(0).c_str());
				else {
					DOLOG(warning, true, "Error by instruction %s", disas_data["instruction-text"].at(0).c_str());
				}

				char *js = json_dumps(test, 0);
				DOLOG(warning, true, "%s\n", js);  // also emit empty line(!)
				free(js);
			}
			else {
				DOLOG(info, true, "\n");  // \n!
				n_ok++;
			}
		}

		// clean-up
		delete b;
	}

	json_decref(json);

	printf("# ok: %zu out of %zu\n", n_ok, array_size);
#endif

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

	std::optional<std::string> dc11_device;

	int  opt          = -1;
	while((opt = getopt(argc, argv, "hD:MT:Br:R:p:ndtL:bl:s:Q:N:J:XS:P1:")) != -1)
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

			case 's': {
					char *c = strchr(optarg, ',');
					if (!c)
						error_exit(false, "-s: parameter missing");
					int bit = atoi(optarg);
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

#if !defined(_WIN32)
	if (validate_json.empty() == false)
		return run_cpu_validation(validate_json);
#endif

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

		b->getCpu()->setRegister(7, bic_start.value());
	}

	if (sa_set)
		b->getCpu()->setRegister(7, start_addr);

	DOLOG(info, true, "Start running at %06o", b->getCpu()->getRegister(7));

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
