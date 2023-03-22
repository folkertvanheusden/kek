// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <atomic>
#include <signal.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <unistd.h>

#include "error.h"
#include "console_ncurses.h"
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
#include "terminal.h"
#include "tty.h"
#include "utils.h"


bool              withUI       { false };
std::atomic_uint32_t event     { 0 };
std::atomic_bool *running      { nullptr };

std::atomic_bool  sigw_event   { false };

void sw_handler(int s)
{
	if (s == SIGWINCH)
		sigw_event = true;
	else {
		fprintf(stderr, "Terminating...\n");

		event = EVENT_TERMINATE;
	}
}

void help()
{
	printf("-h       this help\n");
	printf("-T t.bin load file as a binary tape file (like simh \"load\" command)\n");
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

	uint16_t     start_addr= 01000;
	bool         sa_set    = false;

	std::string  tape;

	uint16_t     console_switches = 0;

	std::string  test;

	disk_backend *temp_d = nullptr;

	int  opt          = -1;
	while((opt = getopt(argc, argv, "hm:T:r:R:p:ndtL:b:l:s:Q:N:")) != -1)
	{
		switch(opt) {
			case 'h':
				help();
				return 1;

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

	setlog(logfile, ll_file, ll_screen);

	bus *b = new bus();

	b->set_console_switches(console_switches);

	cpu *c = new cpu(b, &event);
	b->add_cpu(c);

	std::atomic_bool interrupt_emulation { false };

	if (tape.empty() == false)
		c->setRegister(7, loadTape(b, tape));

	if (sa_set)
		c->setRegister(7, start_addr);

	if (withUI)
		cnsl = new console_ncurses(&event, b);
	else {
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

	tty *tty_ = new tty(cnsl);

	b->add_tty(tty_);

	DOLOG(info, true, "Start running at %o", c->getRegister(7));

	struct sigaction sa { };
	sa.sa_handler = sw_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	if (withUI)
		sigaction(SIGWINCH, &sa, nullptr);

	sigaction(SIGTERM, &sa, nullptr);
	sigaction(SIGINT , &sa, nullptr);

	if (test.empty() == false)
		load_p11_x11(b, test);

	kw11_l *lf = new kw11_l(b, cnsl);

	cnsl->start_thread();

	if (run_debugger || (bootloader == BL_NONE && test.empty()))
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
