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
#include "memory.h"
#include "terminal.h"
#include "tests.h"
#include "tty.h"
#include "utils.h"


typedef enum { BL_NONE, BL_RK05, BL_RL02 } bootloader_t;

bool              withUI       { false };
std::atomic_uint32_t event     { 0 };
std::atomic_bool *running      { nullptr };
bool              trace_output { false };

void loadbin(bus *const b, uint16_t base, const char *const file)
{
	FILE *fh = fopen(file, "rb");

	while(!feof(fh))
		b -> writeByte(base++, fgetc(fh));

	fclose(fh);
}

void setBootLoader(bus *const b, const bootloader_t which)
{
	cpu *const c      = b -> getCpu();

	uint16_t         offset = 0;
	const uint16_t  *bl     = nullptr;
	int              size   = 0;

	if (which == BL_RK05) {
		offset = 01000;

		static uint16_t rk05_code[] = {
			0012700,
			0177406,
			0012710,
			0177400,
			0012740,
			0000005,
			0105710,
			0100376,
			0005007
		};

		bl = rk05_code;

		size = 9;
	}
	else if (which == BL_RL02) {
		offset = 01000;

		/* from https://www.pdp-11.nl/peripherals/disk/rl-info.html
		static uint16_t rl02_code[] = {
			0012701,
			0174400,
			0012761,
			0000013,
			0000004,
			0012711,
			0000004,
			0105711,
			0100376,
			0005061,
			0000002,
			0005061,
			0000004,
			0012761,
			0177400,
			0000006,
			0012711,
			0000014,
			0105711,
			0100376,
			0005007
		};

		size = 21;
		*/

		// from http://gunkies.org/wiki/RL11_disk_controller
		static uint16_t rl02_code[] = {
			0012700,
			0174400,
			0012760,
			0177400,
			0000006,
			0012710,
			0000014,
			0105710,
			0100376,
			0005007,
		};

		size = 10;

		bl = rl02_code;
	}

	for(int i=0; i<size; i++)
		b -> writeWord(offset + i * 2, bl[i]);

	c -> setRegister(7, offset);
}

uint16_t loadTape(bus *const b, const char *const file)
{
	FILE *fh = fopen(file, "rb");
	if (!fh) {
		fprintf(stderr, "Cannot open %s\n", file);
		return -1;
	}

	uint16_t start = 0, end = 0;

	for(;!feof(fh);) {
		uint8_t buffer[6];

		if (fread(buffer, 1, 6, fh) != 6)
			break;

		int count = (buffer[3] << 8) | buffer[2];
		int p = (buffer[5] << 8) | buffer[4];

		uint8_t csum = 0;
		for(int i=2; i<6; i++)
			csum += buffer[i];

		if (count == 6) { // eg no data
			if (p != 1) {
				D(fprintf(stderr, "Setting start address to %o\n", p);)
				start = p;
			}
		}

		D(fprintf(stderr, "%ld] reading %d (dec) bytes to %o (oct)\n", ftell(fh), count - 6, p);)

		for(int i=0; i<count - 6; i++) {
			if (feof(fh)) {
				fprintf(stderr, "short read\n");
				break;
			}
			uint8_t c = fgetc(fh);

			csum += c;
			b -> writeByte(p++, c);

			if (p > end)
				end = p;
		}

		int fcs = fgetc(fh);
		csum += fcs;

		if (csum != 255)
			fprintf(stderr, "checksum error %d\n", csum);
	}

	fclose(fh);

	fh = fopen("test.dat", "wb");
	for(int i=0; i<end; i++)
		fputc(b -> readByte(i), fh);
	fclose(fh);

	return start;
}

void load_p11_x11(bus *const b, const std::string & file)
{
	FILE *fh = fopen(file.c_str(), "rb");
	if (!fh)
		error_exit(true, "Cannot open %s", file.c_str());

	uint16_t addr    = 0;
	int      n = 0;

	while(!feof(fh)) {
		char buffer[4096];

		if (!fgets(buffer, sizeof buffer, fh))
			break;

		if (n) {
			uint8_t byte = strtol(buffer, NULL, 16);

			b->writeByte(addr, byte);

			n--;

			addr++;
		}
		else {
			std::vector<std::string> parts = split(buffer, " ");

			addr = strtol(parts[0].c_str(), NULL, 16);
			n    = strtol(parts[1].c_str(), NULL, 16);
		}
	}

	fclose(fh);

	cpu *const c      = b -> getCpu();
	c -> setRegister(7, 0);
}

volatile bool sw = false;
void sw_handler(int s)
{
	if (s == SIGWINCH)
		sw = true;
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

		*running = true;

		for(;;) {
			while(!event) {
				c->step_a();
				c->step_b();
			}

			uint32_t stop_event = event.exchange(EVENT_NONE);

			if (stop_event == EVENT_HALT || stop_event == EVENT_INTERRUPT || stop_event == EVENT_TERMINATE)
				break;
		}

		auto stats = c->get_mips_rel_speed();

		printf("MIPS: %.2f, running speed: %.2f%%\n", stats.first, stats.second);

		*running = false;
	}

	event = EVENT_TERMINATE;

	delete cnsl;

	delete b;

	delete lf;

	return 0;
}
