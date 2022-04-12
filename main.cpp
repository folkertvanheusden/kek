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
#include "memory.h"
#include "terminal.h"
#include "tests.h"
#include "tty.h"
#include "utils.h"


bool              withUI       { false };
uint32_t          event        { 0 };
std::atomic_bool  terminate    { false };
std::atomic_bool *running      { nullptr };
bool              trace_output { false };

void loadbin(bus *const b, uint16_t base, const char *const file)
{
	FILE *fh = fopen(file, "rb");

	while(!feof(fh))
		b -> writeByte(base++, fgetc(fh));

	fclose(fh);
}

void setBootLoader(bus *const b)
{
	cpu *const c = b -> getCpu();

	const uint16_t offset = 01000;
	constexpr uint16_t bootrom[] = {
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

	FILE *fh = fopen("boot.dat", "wb");

	for(size_t i=0; i<sizeof bootrom / 2; i++) {
		b -> writeWord(offset + i * 2, bootrom[i]);
		fputc(bootrom[i] & 255, fh);
		fputc(bootrom[i] >> 8, fh);
	}

	fclose(fh);

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

volatile bool sw = false;
void sw_handler(int s)
{
	if (s == SIGWINCH)
		sw = true;
	else {
		fprintf(stderr, "Terminating...\n");

		terminate = true;
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

	c -> setEmulateMFPT(true);

	std::vector<std::string> rk05_files;
	bool testCases    = false;
	bool run_debugger = false;
	bool tracing      = false;
	int  opt          = -1;
	while((opt = getopt(argc, argv, "hm:T:R:p:ndtL:")) != -1)
	{
		switch(opt) {
			case 'h':
				help();
				return 1;

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
		cnsl = new console_ncurses(&terminate, &interrupt_emulation, b);
	else {
		fprintf(stderr, "This PDP-11 emulator is called \"kek\" (reason for that is forgotten) and was written by Folkert van Heusden.\n");

		fprintf(stderr, "Built on: " __DATE__ " " __TIME__ "\n");

		cnsl = new console_posix(&terminate, &interrupt_emulation, b);
	}

	if (rk05_files.empty() == false) {
		b->add_rk05(new rk05(rk05_files, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

		setBootLoader(b);
	}

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

	*running = true;

//	loadbin(b, 0, "test.dat");
//	c->setRegister(7, 0);

	c->emulation_start();  // for statistics

	cnsl->start_thread();

	if (run_debugger)
		debugger(cnsl, b, &interrupt_emulation, tracing);
	else {
		while(!event && !terminate)
			c->step();
	}

	*running = false;

	terminate = true;

	delete cnsl;

	delete b;

	return 0;
}
