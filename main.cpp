// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <assert.h>
#include <atomic>
#include <jansson.h>
#include <signal.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <unistd.h>

#include "error.h"
#if !defined(_WIN32)
#include "console_ncurses.h"
#endif
#include "console_posix.h"
#include "cpu.h"
#include "debugger.h"
#include "disk_backend.h"
#include "disk_backend_file.h"
#include "disk_backend_nbd.h"
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
#endif

int run_cpu_validation(const std::string & filename)
{
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
			c->lowlevel_psw_set(json_integer_value(psw_reg) & 0174377);
		}
		{
			json_t *b_pc = json_object_get(registers_before, "pc");
			assert(b_pc);
			start_pc = json_integer_value(b_pc);
			c->setPC(start_pc);
		}
		{
			json_t *b_sp = json_object_get(registers_before, "sp");
			size_t array_size = json_array_size(b_sp);
			assert(array_size == 4);
			for(size_t i=0; i<array_size; i++) {
				json_t *temp = json_array_get(b_sp, i);
				c->lowlevel_register_sp_set(i, json_integer_value(temp));
			}
		}

		c->step_a();
		disassemble(c, nullptr, start_pc, false);
		auto disas_data = c->disassemble(start_pc);
		c->step_b();

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

			// TODO check SP[]
			{
				json_t *a_sp = json_object_get(registers_after, "sp");
				size_t array_size = json_array_size(a_sp);
				assert(array_size == 4);
				for(size_t i=0; i<array_size; i++) {
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
				uint16_t should_be_psw = json_integer_value(a_psw) & 0174377;
				if (should_be_psw != psw) {
					DOLOG(warning, true, "PSW register mismatch (is: %06o (%d), should be: %06o (%d))", psw, psw, should_be_psw, should_be_psw);
					err = true;
				}
			}

			if (err) {
				if (c->is_it_a_trap())
					DOLOG(warning, true, "Error by TRAP");
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

	return 0;
}

void help()
{
	printf("-h       this help\n");
	printf("-T t.bin load file as a binary tape file (like simh \"load\" command), also for .BIC files\n");
	printf("-B       run tape file as a unit test (for .BIC files)\n");
	printf("-R d.rk  load file as a RK05 disk device\n");
	printf("-r d.rl  load file as a RL02 disk device\n");
	printf("-N host:port:type  use NBD-server as disk device, type being either \"rk05\" or \"rl02\"\n");
	printf("-p 123   set CPU start pointer to decimal(!) value\n");
	printf("-b x     enable bootloader (build-in), parameter must be \"rk05\" or \"rl02\"\n");
	printf("-n       ncurses UI\n");
	printf("-d       enable debugger\n");
	printf("-s x,y   set console switche state: set bit x (0...15) to y (0/1)\n");
	printf("-t       enable tracing (disassemble to stderr, requires -d as well)\n");
	printf("-l x     log to file x\n");
	printf("-L x,y   set log level for screen (x) and file (y)\n");
	printf("-X       do not include timestamp in logging\n");
	printf("-J x     run validation suite x against the CPU emulation\n");
}

int main(int argc, char *argv[])
{
	//setlocale(LC_ALL, "");

	std::vector<disk_backend *> rk05_files;
	std::vector<disk_backend *> rl02_files;

	bool run_debugger = false;
	bool tracing      = false;

	bootloader_t  bootloader = BL_NONE;

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

	disk_backend *temp_d = nullptr;

	std::string  validate_json;

	int  opt          = -1;
	while((opt = getopt(argc, argv, "hm:T:Br:R:p:ndtL:b:l:s:Q:N:J:X")) != -1)
	{
		switch(opt) {
			case 'h':
				help();
				return 1;

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
				if (strcasecmp(optarg, "rk05") == 0)
					bootloader = BL_RK05;
				else if (strcasecmp(optarg, "rl02") == 0)
					bootloader = BL_RL02;
				else
					error_exit(false, "Bootload \"%s\" not recognized", optarg);

				break;

			case 'd':
				run_debugger = true;
				break;

			case 't':
				tracing      = true;
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
				temp_d = new disk_backend_file(optarg);
				if (!temp_d->begin())
					error_exit(false, "Cannot use file \"%s\" for RK05", optarg);
				rk05_files.push_back(temp_d);
				break;

			case 'r':
				temp_d = new disk_backend_file(optarg);
				if (!temp_d->begin())
					error_exit(false, "Cannot use file \"%s\" for RL02", optarg);
				rl02_files.push_back(temp_d);
				break;

			case 'N': {
					  auto parts = split(optarg, ":");
					  if (parts.size() != 3)
						  error_exit(false, "-N: parameter missing");

					  temp_d = new disk_backend_nbd(parts.at(0), atoi(parts.at(1).c_str()));

					  if (parts.at(2) == "rk05")
						rk05_files.push_back(temp_d);
					  else if (parts.at(2) == "rl02")
						rl02_files.push_back(temp_d);
					  else
					  	error_exit(false, "\"%s\" is not recognized as a disk type", parts.at(2).c_str());
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

			default:
			        fprintf(stderr, "-%c is not understood\n", opt);
				return 1;
		}
	}

	console *cnsl = nullptr;

	setlog(logfile, ll_file, ll_screen, timestamp);

	if (validate_json.empty() == false)
		return run_cpu_validation(validate_json);

	bus *b = new bus();

	b->set_console_switches(console_switches);

	cpu *c = new cpu(b, &event);
	b->add_cpu(c);

	std::atomic_bool interrupt_emulation { false };

	std::optional<uint16_t> bic_start;

	if (tape.empty() == false) {
		bic_start = loadTape(b, tape);

		if (bic_start.has_value() == false)
			return 1;  // fail

		c->setRegister(7, bic_start.value());
	}

	if (sa_set)
		c->setRegister(7, start_addr);

#if !defined(_WIN32)
	if (withUI)
		cnsl = new console_ncurses(&event, b);
	else
#endif
	{
		DOLOG(info, true, "This PDP-11 emulator is called \"kek\" (reason for that is forgotten) and was written by Folkert van Heusden.");

		DOLOG(info, true, "Built on: " __DATE__ " " __TIME__);

		cnsl = new console_posix(&event, b);
	}

	if (rk05_files.empty() == false) {
		if (bootloader != BL_RK05)
			DOLOG(warning, true, "Note: loading RK05 with no RK05 bootloader selected");

		b->add_rk05(new rk05(rk05_files, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));
	}

	if (rl02_files.empty() == false) {
		if (bootloader != BL_RL02)
			DOLOG(warning, true, "Note: loading RL02 with no RL02 bootloader selected");

		b->add_rl02(new rl02(rl02_files, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));
	}

	if (bootloader != BL_NONE)
		setBootLoader(b, bootloader);

	running = cnsl->get_running_flag();

	tty *tty_ = new tty(cnsl, b);

	b->add_tty(tty_);

	DOLOG(info, true, "Start running at %06o", c->getRegister(7));

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

	kw11_l *lf = new kw11_l(b, cnsl);

	cnsl->start_thread();

	if (is_bic)
		run_bic(cnsl, b, &event, tracing, bic_start.value());
	else if (run_debugger || (bootloader == BL_NONE && test.empty()))
		debugger(cnsl, b, &event, tracing);
	else {
		c->emulation_start();  // for statistics

		for(;;) {
			*running = true;

			while(event == EVENT_NONE) {
				c->step_a();
				c->step_b();
			}

			*running = false;

			uint32_t stop_event = event.exchange(EVENT_NONE);

			if (stop_event == EVENT_HALT || stop_event == EVENT_INTERRUPT || stop_event == EVENT_TERMINATE)
				break;
		}

		auto stats = c->get_mips_rel_speed();

		cnsl->put_string_lf(format("MIPS: %.2f, relative speed: %.2f%%, instructions executed: %lu", std::get<0>(stats), std::get<1>(stats), std::get<2>(stats)));
	}

	event = EVENT_TERMINATE;

	delete cnsl;

	delete b;

	delete lf;

	return 0;
}
