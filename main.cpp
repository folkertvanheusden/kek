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
#include "gen.h"
#include "kw11-l.h"
#include "loaders.h"
#include "memory.h"
#include "terminal.h"
#include "tests.h"
#include "tty.h"
#include "utils.h"


bool              withUI       { false };
std::atomic_uint32_t event     { 0 };
std::atomic_bool *running      { nullptr };
bool              trace_output { false };

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
	printf("-m mode  \"tc\": run testcases\n");
	printf("-T t.bin load file as a binary tape file (like simh \"load\" command)\n");
	printf("-R d.rk  load file as a RK05 disk device\n");
	printf("-p 123   set CPU start pointer to decimal(!) value\n");
	printf("-L f.bin load file into memory at address given by -p (and run it)\n");
	printf("-b x     enable bootloader (build-in), parameter must be \"rk05\" or \"rl02\"\n");
	printf("-n       ncurses UI\n");
	printf("-d       enable debugger\n");
	printf("-t       enable tracing (disassemble to stderr, requires -d as well)\n");
}

int main(int argc, char *argv[])
{
	//setlocale(LC_ALL, "");

	bus *b = new bus();

	cpu *c = new cpu(b, &event);
	b->add_cpu(c);

	kw11_l *lf = new kw11_l(b);

	c -> setEmulateMFPT(true);

	std::vector<std::string> rk05_files;
	std::vector<std::string> rl02_files;

	bool testCases    = false;
	bool run_debugger = false;
	bool tracing      = false;

	bootloader_t  bootloader = BL_NONE;

	int  opt          = -1;
	while((opt = getopt(argc, argv, "hm:T:r:R:p:ndtL:b:")) != -1)
	{
		switch(opt) {
			case 'h':
				help();
				return 1;

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
				trace_output = true;
				break;

			case 'n':
				withUI = true;
				break;

			case 'm':
				if (strcasecmp(optarg, "tc") == 0)
					testCases = true;
				else {
					fprintf(stderr, "\"-m %s\" is not known\n", optarg);
					return 1;
				}
				break;

			case 'T':
				c->setRegister(7, loadTape(b, optarg));
				break;

			case 'R':
				rk05_files.push_back(optarg);
				break;

			case 'r':
				rl02_files.push_back(optarg);
				break;

			case 'p':
				c->setRegister(7, atoi(optarg));
				break;

			case 'L':
				loadbin(b, c->getRegister(7), optarg);
				break;

			default:
				  fprintf(stderr, "-%c is not understood\n", opt);
				  return 1;
		}
	}

	console *cnsl = nullptr;

	std::atomic_bool interrupt_emulation { false };

	if (withUI)
		cnsl = new console_ncurses(&event, b);
	else {
		fprintf(stderr, "This PDP-11 emulator is called \"kek\" (reason for that is forgotten) and was written by Folkert van Heusden.\n");

		fprintf(stderr, "Built on: " __DATE__ " " __TIME__ "\n");

		cnsl = new console_posix(&event, b);
	}

	if (rk05_files.empty() == false)
		b->add_rk05(new rk05(rk05_files, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

	if (rl02_files.empty() == false)
		b->add_rl02(new rl02(rl02_files, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

	if (bootloader != BL_NONE)
		setBootLoader(b, bootloader);

	running = cnsl->get_running_flag();

	tty *tty_ = new tty(cnsl);

	b->add_tty(tty_);

	if (testCases)
		tests(c);

	D(fprintf(stderr, "Start running at %o\n", c->getRegister(7));)

	struct sigaction sa { };
	sa.sa_handler = sw_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	if (withUI)
		sigaction(SIGWINCH, &sa, nullptr);

	sigaction(SIGTERM, &sa, nullptr);
	sigaction(SIGINT , &sa, nullptr);

//	loadbin(b, 0, "test.dat");
//	c->setRegister(7, 0);

//load_p11_x11(b, "/home/folkert/Projects/PDP-11/work/p11-2.10i/Tests/mtpi.x11");

	cnsl->start_thread();

	if (run_debugger)
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

		printf("MIPS: %.2f, running speed: %.2f%%\n", stats.first, stats.second);
	}

	event = EVENT_TERMINATE;

	delete cnsl;

	delete b;

	delete lf;

	return 0;
}
