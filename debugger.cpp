// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include "bus.h"
#include "console.h"
#include "cpu.h"
#include "gen.h"
#include "log.h"
#include "tty.h"
#include "utils.h"


#if defined(ESP32) || defined(BUILD_FOR_RP2040)
#if defined(ESP32)
#include "esp32.h"
#elif defined(BUILD_FOR_RP2040)
#include "rp2040.h"
#endif

void setBootLoader(bus *const b);

void configure_disk(console *const c);

void configure_network(console *const c);
void check_network(console *const c);
void start_network(console *const c);

void set_tty_serial_speed(console *const c, const uint32_t bps);

void recall_configuration(console *const c);
#endif

// returns size of instruction (in bytes)
int disassemble(cpu *const c, console *const cnsl, const uint16_t pc, const bool instruction_only)
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
		result = format("R0: %s, R1: %s, R2: %s, R3: %s, R4: %s, R5: %s, SP: %s, PC: %06o, PSW: %s, instr: %s: %s - MMR0/1/2/3: %s/%s/%s/%s",
				registers[0].c_str(), registers[1].c_str(), registers[2].c_str(), registers[3].c_str(), registers[4].c_str(), registers[5].c_str(),
				registers[6].c_str(), pc, 
				psw.c_str(),
				instruction_values.c_str(),
				instruction.c_str(),
				MMR0.c_str(), MMR1.c_str(), MMR2.c_str(), MMR3.c_str()
				);
#if defined(COMPARE_OUTPUT)
	{
		std::string temp = format("R0: %s, R1: %s, R2: %s, R3: %s, R4: %s, R5: %s, SP: %s, PC: %06o, PSW: %s, instr: %s",
				registers[0].c_str(), registers[1].c_str(), registers[2].c_str(), registers[3].c_str(), registers[4].c_str(), registers[5].c_str(), registers[6].c_str(), pc, 
				psw.c_str(),
				data["instruction-values"][0].c_str()
				);

		FILE *fh = fopen("compare.dat", "a+");
		fprintf(fh, "%s\n", temp.c_str());
		fclose(fh);
	}
#endif

	if (cnsl)
		cnsl->debug(result);
	else
		DOLOG(debug, true, "%s", result.c_str());

	std::string sp;
	for(auto sp_val : data["sp"])
		sp += (sp.empty() ? "" : ",") + sp_val;

	DOLOG(debug, true, "SP: %s", sp.c_str());

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

void dump_par_pdr(console *const cnsl, bus *const b, const uint16_t pdrs, const uint16_t pars, const std::string & name, const int state)
{
	if (state == 0 || state == 2)
		cnsl->put_string_lf(name);
	else
		cnsl->put_string_lf(format("%s DISABLED", name.c_str()));

	cnsl->put_string_lf("   PAR             PDR");

	for(int i=0; i<8; i++) {
		uint16_t par_value = b->read(pars + i * 2, wm_word, rm_cur, true);
		uint16_t pdr_value = b->read(pdrs + i * 2, wm_word, rm_cur, true);

		uint16_t pdr_len   = (((pdr_value >> 8) & 127) + 1) * 64;

		cnsl->put_string_lf(format("%d] %06o %08o %06o %04o D%d A%d", i, par_value, par_value * 64, pdr_value, pdr_len, !!(pdr_value & 8), pdr_value & 7));
	}
}

void mmu_dump(console *const cnsl, bus *const b)
{
	uint16_t mmr0 = b->getMMR0();

	cnsl->put_string_lf(mmr0 & 1 ? "MMU enabled" : "MMU NOT enabled");

	uint16_t mmr3 = b->getMMR3();

	cnsl->put_string_lf(format("MMR0: %06o", mmr0));
	cnsl->put_string_lf(format("MMR1: %06o", b->getMMR1()));
	cnsl->put_string_lf(format("MMR2: %06o", b->getMMR2()));
	cnsl->put_string_lf(format("MMR3: %06o", mmr3));

	dump_par_pdr(cnsl, b, ADDR_PDR_SV_START,       ADDR_PAR_SV_START,       "supervisor i-space", 0);
	dump_par_pdr(cnsl, b, ADDR_PDR_SV_START + 020, ADDR_PAR_SV_START + 020, "supervisor d-space", 1 + (!!(mmr3 & 2)));

	dump_par_pdr(cnsl, b, ADDR_PDR_K_START,       ADDR_PAR_K_START,       "kernel i-space", 0);
	dump_par_pdr(cnsl, b, ADDR_PDR_K_START + 020, ADDR_PAR_K_START + 020, "kernel d-space", 1 + (!!(mmr3 & 4)));

	dump_par_pdr(cnsl, b, ADDR_PDR_U_START,       ADDR_PAR_U_START,       "user i-space", 0);
	dump_par_pdr(cnsl, b, ADDR_PDR_U_START + 020, ADDR_PAR_U_START + 020, "user d-space", 1 + (!!(mmr3 & 1)));
}

void show_run_statistics(console *const cnsl, cpu *const c)
{
	auto stats = c->get_mips_rel_speed();

	cnsl->put_string_lf(format("Executed %zu instructions in %u ms of which %.2f ms idle", size_t(std::get<2>(stats)), std::get<3>(stats), std::get<4>(stats)));
	cnsl->put_string_lf(format("MIPS: %.2f, relative speed: %.2f%%", std::get<0>(stats), std::get<1>(stats)));
}

void debugger(console *const cnsl, bus *const b, std::atomic_uint32_t *const stop_event, const bool tracing_in)
{
	int32_t trace_start_addr = -1;
	bool    tracing          = tracing_in;
	int     n_single_step    = 1;
	bool    turbo            = false;

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
					n_single_step = atoi(parts[1].c_str());
				else
					n_single_step = 1;

				*stop_event = EVENT_NONE;
			}
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

				for(auto a : bps) {
					cnsl->put_string(format("   %06o> ", a));

					disassemble(c, cnsl, a, true);
				}

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

					c->getBus()->writeByte(a, v);

					cnsl->put_string_lf(format("Set %06o to %03o", a, v));
				}

				continue;
			}
			else if (parts[0] == "trace" || parts[0] == "t") {
				tracing = !tracing;

				cnsl->put_string_lf(format("Tracing set to %s", tracing ? "ON" : "OFF"));

				continue;
			}
			else if (parts[0] == "mmudump") {
				mmu_dump(cnsl, b);

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
					uint16_t addr   = std::stoi(parts[2], nullptr, 8);
					int  val        = -1;

					int  n          = parts.size() == 4 ? atoi(parts[3].c_str()) : 1;
					bool word       = parts[1] == "w";

					if (parts[1] != "w" && parts[1] != "b") {
						cnsl->put_string_lf("expected b or w");

						continue;
					}

					std::string out;

					for(int i=0; i<n; i++) {
						if (!word)
							val = b->read(addr + i, wm_byte, rm_cur, true);
						else if (word)
							val = b->read(addr + i, wm_word, rm_cur, true);

						if (val == -1) {
							cnsl->put_string_lf(format("Can't read from %06o\n", addr + i));
							break;
						}

						if (n == 1)
							cnsl->put_string_lf(format("value at %06o, octal: %o, hex: %x, dec: %d\n", addr + i, val, val, val));

						if (n > 1) {
							if (i > 0)
								out += " ";

							if (word)
								out += format("%06o", val);
							else
								out += format("%03o", val);
						}
					}

					if (n > 1)
						cnsl->put_string_lf(out);
				}

				continue;
			}
			else if (cmd == "reset" || cmd == "r") {
#if defined(ESP32)
				ESP.restart();
#else
				*stop_event = EVENT_NONE;

				c->reset();
#endif
				continue;
			}
#if defined(ESP32) || defined(BUILD_FOR_RP2040)
			else if (cmd == "cfgdisk") {
				configure_disk(cnsl);

				continue;
			}
#endif
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
			else if (parts.at(0) == "serspd") {
				if (parts.size() == 2) {
					uint32_t speed = std::stoi(parts.at(1), nullptr, 10);
					set_tty_serial_speed(cnsl, speed);

					cnsl->put_string_lf(format("Set baudrate to %d", speed));
				}
				else {
					cnsl->put_string_lf("serspd requires an (decimal) parameter");
				}

				continue;
			}
			else if (cmd == "init") {
				recall_configuration(cnsl);

				continue;
			}
			else if (cmd == "stats") {
				show_run_statistics(cnsl, c);

				continue;
			}
#endif
			else if (cmd == "cls") {
				const char cls[] = { 27, '[', '2', 'J', 12, 0 };

				cnsl->put_string_lf(cls);

				continue;
			}
			else if (cmd == "turbo") {
				turbo = !turbo;

				cnsl->put_string_lf(format("Turbo set to %s", turbo ? "ON" : "OFF"));

				continue;
			}
			else if (cmd == "quit" || cmd == "q") {
#if defined(ESP32)
				ESP.restart();
#endif
				break;
			}
			else if (cmd == "help" || cmd == "h" || cmd == "?") {
				cnsl->put_string_lf("disassemble/d - show current instruction (pc=/n=)");
				cnsl->put_string_lf("go            - run until trap or ^e");
#if !defined(ESP32)
				cnsl->put_string_lf("quit/q        - stop emulator");
#endif
				cnsl->put_string_lf("examine/e     - show memory address (<b|w> <octal address> [<n>])");
				cnsl->put_string_lf("reset/r       - reset cpu/bus/etc");
				cnsl->put_string_lf("single/s      - run 1 instruction (implicit 'disassemble' command)");
				cnsl->put_string_lf("sbp/cbp/lbp   - set/clear/list breakpoint(s)");
				cnsl->put_string_lf("trace/t       - toggle tracing");
				cnsl->put_string_lf("turbo         - toggle turbo mode (cannot be interrupted)");
				cnsl->put_string_lf("strace        - start tracing from address - invoke without address to disable");
				cnsl->put_string_lf("mmudump       - dump MMU settings (PARs/PDRs)");
				cnsl->put_string_lf("setpc         - set PC to value");
				cnsl->put_string_lf("setmem        - set memory (a=) to value (v=), both in octal, one byte");
				cnsl->put_string_lf("toggle        - set switch (s=, 0...15 (decimal)) of the front panel to state (t=, 0 or 1)");
				cnsl->put_string_lf("cls           - clear screen");
				cnsl->put_string_lf("stats         - show run statistics");
#if defined(ESP32)
				cnsl->put_string_lf("cfgnet        - configure network (e.g. WiFi)");
				cnsl->put_string_lf("startnet      - start network");
				cnsl->put_string_lf("chknet        - check network status");
				cnsl->put_string_lf("serspd        - set serial speed in bps (8N1 are default)");
				cnsl->put_string_lf("init          - reload (disk-)configuration from flash");
#endif
#if defined(ESP32) || defined(BUILD_FOR_RP2040)
				cnsl->put_string_lf("cfgdisk       - configure disk");
#endif

				continue;
			}
			else {
				cnsl->put_string_lf("?");
				continue;
			}

			c->emulation_start();

			*cnsl->get_running_flag() = true;

			if (turbo) {
				while(*stop_event == EVENT_NONE) {
					c->step_a();
					c->step_b();
				}
			}
			else {
				while(*stop_event == EVENT_NONE) {
					if (!single_step)
						DOLOG(debug, false, "---");

					c->step_a();

					if (trace_start_addr != -1 && c->getPC() == trace_start_addr)
						tracing = true;

					if (tracing || single_step)
						disassemble(c, single_step ? cnsl : nullptr, c->getPC(), false);

					if (c->check_breakpoint() && !single_step) {
						cnsl->put_string_lf("Breakpoint");
						break;
					}

					c->step_b();

					if (single_step && --n_single_step == 0)
						break;
				}
			}

			*cnsl->get_running_flag() = false;

			c->reset();
		}
		catch(const std::exception & e) {
			cnsl->put_string_lf(format("Exception caught: %s", e.what()));
		}
		catch(...) {
			cnsl->put_string_lf("Unspecified exception caught");
		}
	}
}

void run_bic(console *const cnsl, bus *const b, std::atomic_uint32_t *const stop_event, const bool tracing, const uint16_t start_addr)
{
	cpu *const c = b->getCpu();

	c->setRegister(7, start_addr);

	*cnsl->get_running_flag() = true;

	while(*stop_event == EVENT_NONE) {
		c->step_a();

		if (tracing)
			disassemble(c, nullptr, c->getPC(), false);

		c->step_b();
	}

	*cnsl->get_running_flag() = false;
}
