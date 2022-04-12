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

// returns size of instruction (in bytes)
int disassemble(cpu *const c, console *const cnsl, const int pc, const bool instruction_only)
{
	auto data      = c->disassemble(pc);

	auto registers = data["registers"];
	auto psw       = data["psw"][0];

	std::string instruction_values;
	for(auto iv : data["instruction-values"])
		instruction_values += (instruction_values.empty() ? "" : ",") + iv;

	std::string work_values;
	for(auto wv : data["work-values"])
		work_values += (work_values.empty() ? "" : ",") + wv;

	std::string instruction = data["instruction-text"].at(0);

	std::string result;

	if (instruction_only)
		result = format("PC: %06o, instr: %s\t%s\t%s",
				pc,
				instruction_values.c_str(),
				instruction.c_str(),
				work_values.c_str()
				);
	else
		result = format("R0: %s, R1: %s, R2: %s, R3: %s, R4: %s, R5: %s, SP: %s, PC: %06o, PSW: %s, instr: %s: %s - %s",
				registers[0].c_str(), registers[1].c_str(), registers[2].c_str(), registers[3].c_str(), registers[4].c_str(), registers[5].c_str(),
				registers[6].c_str(), pc, 
				psw.c_str(),
				instruction_values.c_str(),
				instruction.c_str(),
				work_values.c_str()
				);

	if (cnsl)
		cnsl->debug(result);
	else
		fprintf(stderr, "%s\n", result.c_str());

	return data["instruction-values"].size() * 2;
}

std::map<std::string, std::string> split(const std::vector<std::string> & kv_array, const std::string & splitter)
{
	std::map<std::string, std::string> out;

	for(auto pair : kv_array) {
		auto kv = split(pair, splitter);

		if (kv.size() == 1)
			out.insert({ kv[0], "" });
		else if (kv.size() == 2)
			out.insert({ kv[0], kv[1] });
	}

	return out;
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
	bool testCases = false;
	bool debugger  = false;
	bool tracing   = false;
	int  opt       = -1;
	while((opt = getopt(argc, argv, "hm:T:R:p:ndtL:")) != -1)
	{
		switch(opt) {
			case 'h':
				help();
				return 1;

			case 'd':
				debugger = true;
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

	if (debugger) {
		bool single_step = false;

		while(!terminate) {
			bool        temp  = terminate;
			std::string cmd   = cnsl->read_line(format("%d%d", event, temp));
			auto        parts = split(cmd, " ");
			auto        kv    = split(parts, "=");

			if (parts.empty())
				continue;

			if (cmd == "go")
				single_step = false;
			else if (cmd == "single" || cmd == "s")
				single_step = true;
			else if ((parts[0] == "sbp" || parts[0] == "cbp") && parts.size() == 2){
				uint16_t pc = std::stoi(parts[1].c_str(), nullptr, 8);

				if (parts[0] == "sbp") {
					c->set_breakpoint(pc);

					cnsl->put_string_lf(format("Set breakpoint at %06o", pc));
				}
				else {
					c->remove_breakpoint(pc);

					cnsl->put_string_lf(format("Clear breakpoint at %06o", pc));
				}

				continue;
			}
			else if (cmd == "lbp") {
				auto bps = c->list_breakpoints();

				cnsl->put_string_lf("Breakpoints:");

				for(auto pc : bps) {
					cnsl->put_string_lf(format("   %06o", pc));

					pc += disassemble(c, cnsl, pc, true);
				}

				continue;
			}
			else if (parts[0] == "disassemble" || parts[0] == "d") {
				int pc = kv.find("pc") != kv.end() ? std::stoi(kv.find("pc")->second, nullptr, 8)  : c->getPC();
				int n  = kv.find("n")  != kv.end() ? std::stoi(kv.find("n") ->second, nullptr, 10) : 1;

				for(int i=0; i<n; i++)
					pc += disassemble(c, cnsl, pc, true);

				continue;
			}
			else if (cmd == "reset" || cmd == "r") {
				terminate = false;

				event = 0;

				c->reset();

				continue;
			}
			else if (cmd == "quit" || cmd == "q")
				break;
			else if (cmd == "help") {
				cnsl->put_string_lf("disassemble/d - show current instruction (pc=/n=)");
				cnsl->put_string_lf("go            - run until trap or ^e");
				cnsl->put_string_lf("quit/q        - stop emulator");
				cnsl->put_string_lf("reset/r       - reset cpu/bus/etc");
				cnsl->put_string_lf("single/s      - run 1 instruction (implicit 'disassemble' command)");
				cnsl->put_string_lf("sbp/cbp/lbp   - set/clear/list breakpoint(s)");

				continue;
			}
			else {
				cnsl->put_string_lf("?");
				continue;
			}

			while(!event && !terminate && !interrupt_emulation) {
				if (tracing || single_step)
					disassemble(c, single_step ? cnsl : nullptr, c->getPC(), false);

				if (c->check_breakpoint() && !single_step) {
					cnsl->put_string_lf("Breakpoint");
					break;
				}

				c->step();

				if (single_step)
					break;
			}

			if (interrupt_emulation) {
				single_step = true;

				event = 0;

				interrupt_emulation = false;
			}
		}
	}
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
