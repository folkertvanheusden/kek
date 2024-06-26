// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "breakpoint.h"
#include "bus.h"
#include "cpu.h"
#include "gen.h"
#include "log.h"
#include "utils.h"


#define SIGN(x, wm) ((wm) == wm_byte ? (x) & 0x80 : (x) & 0x8000)

#define IS_0(x, wm) ((wm) == wm_byte ? ((x) & 0xff) == 0 : (x) == 0)

// see https://retrocomputing.stackexchange.com/questions/6960/what-was-the-clock-speed-and-ips-for-the-original-pdp-11
constexpr const double pdp11_clock_cycle = 150;  // ns, for the 11/70
constexpr const double pdp11_MHz = 1000.0 / pdp11_clock_cycle;
constexpr const double pdp11_avg_cycles_per_instruction = (1 + 5) / 2.0;
constexpr const double pdp11_estimated_mips = pdp11_MHz / pdp11_avg_cycles_per_instruction;

cpu::cpu(bus *const b, std::atomic_uint32_t *const event) : b(b), event(event)
{
	reset();

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(qi_lock);  // initialize
#endif
}

cpu::~cpu()
{
}

void cpu::init_interrupt_queue()
{
	queued_interrupts.clear();

	for(uint8_t level=0; level<8; level++)
		queued_interrupts.insert({ level, { } });
}

void cpu::emulation_start()
{
	instruction_count = 0;

	running_since = get_us();
	wait_time     = 0;
}

std::optional<std::string> cpu::check_breakpoint()
{
	for(auto & bp: breakpoints) {
		auto rc = bp.second->is_triggered();
		if (rc.has_value())
			return rc;
	}

	return { };
}

int cpu::set_breakpoint(breakpoint *const bp)
{
	breakpoints.insert({ ++bp_nr, bp });

	return bp_nr;
}

bool cpu::remove_breakpoint(const int bp_id)
{
	auto it = breakpoints.find(bp_id);
	if (it == breakpoints.end())
		return false;

	delete it->second;

	return breakpoints.erase(bp_id) == 1;
}

std::map<int, breakpoint *> cpu::list_breakpoints()
{
	return breakpoints;
}

uint64_t cpu::get_instructions_executed_count() const
{
	// this may wreck havoc as it is not protected by a mutex
	// but a mutex would slow things down too much (as would
	// do an atomic)
	return instruction_count;
}

std::tuple<double, double, uint64_t, uint32_t, double> cpu::get_mips_rel_speed(const std::optional<uint64_t> & instruction_count, const std::optional<uint64_t> & t_diff_in) const
{
	uint64_t instr_count = instruction_count.has_value() ? instruction_count.value() : get_instructions_executed_count();

        uint64_t t_diff = t_diff_in.has_value() ? t_diff_in.value() : (get_us() - running_since - wait_time);

        double mips = t_diff ? instr_count / double(t_diff) : 0;

	return { mips, mips * 100 / pdp11_estimated_mips, instr_count, t_diff, wait_time };
}

uint32_t cpu::get_effective_run_time(const uint64_t instruction_count) const
{
	// division is to go from ns to ms
	return instruction_count * pdp11_avg_cycles_per_instruction * pdp11_clock_cycle / 1000000l;
}

void cpu::add_to_stack_trace(const uint16_t p)
{
	auto da = disassemble(p);

	stacktrace.push_back({ p, da["instruction-text"][0] });

	while (stacktrace.size() >= max_stacktrace_depth)
		stacktrace.erase(stacktrace.begin());
}

void cpu::pop_from_stack_trace()
{
	if (!stacktrace.empty())
		stacktrace.pop_back();
}

std::vector<std::pair<uint16_t, std::string> > cpu::get_stack_trace() const
{
	return stacktrace;
}

void cpu::reset()
{
	memset(regs0_5, 0x00, sizeof regs0_5);
	memset(sp,      0x00, sizeof sp);
	pc   = 0;
	psw  = 0;  // 7 << 5;
	fpsr = 0;
	init_interrupt_queue();
}

uint16_t cpu::get_register(const int nr) const
{
	if (nr < 6) {
		int set = get_register_set();

		return regs0_5[set][nr];
	}

	if (nr == 6)
		return sp[getPSW_runmode()];

	assert(nr == 7);

	return pc;
}

void cpu::set_register(const int nr, const uint16_t value)
{
	if (nr < 6) {
		int set = get_register_set();

		regs0_5[set][nr] = value;
	}
	else if (nr == 6)
		sp[getPSW_runmode()] = value;
	else {
		assert(nr == 7);
		pc = value;
	}
}

void cpu::set_registerLowByte(const int nr, const word_mode_t word_mode, const uint16_t value)
{
	if (word_mode == wm_byte) {
		uint16_t v = get_register(nr);

		v &= 0xff00;

		assert(value < 256);
		v |= value;

		set_register(nr, v);
	}
	else {
		set_register(nr, value);
	}
}

bool cpu::put_result(const gam_rc_t & g, const uint16_t value)
{
	if (g.addr.has_value() == false) {
		set_registerLowByte(g.reg.value(), g.word_mode, value);

		return true;
	}

	return b->write(g.addr.value(), g.word_mode, value, g.mode_selection, g.space) == false;
}

uint16_t cpu::add_register(const int nr, const uint16_t value)
{
	if (nr < 6)
		return regs0_5[get_register_set()][nr] += value;

	if (nr == 6)
		return sp[getPSW_runmode()] += value;

	assert(nr == 7);

	return pc += value;
}

void cpu::lowlevel_register_set(const uint8_t set, const uint8_t reg, const uint16_t value)
{
	assert(set < 2);
	assert(reg < 8);

	if (reg < 6)
		regs0_5[set][reg] = value;
	else if (reg == 6)
		sp[set == 0 ? 0 : 3] = value;
	else {
		assert(reg == 7);
		pc = value;
	}
}

uint16_t cpu::lowlevel_register_get(const uint8_t set, const uint8_t reg)
{
	assert(set < 2);
	assert(reg < 8);

	if (reg < 6)
		return regs0_5[set][reg];

	if (reg == 6)
		return sp[set == 0 ? 0 : 3];

	assert(reg == 7);

	return pc;
}

void cpu::lowlevel_register_sp_set(const uint8_t set, const uint16_t value)
{
	assert(set < 4);
	sp[set] = value;
}

bool cpu::getBitPSW(const int bit) const
{
	return (psw >> bit) & 1;
}

bool cpu::getPSW_c() const
{
	return getBitPSW(0);
}

bool cpu::getPSW_v() const
{
	return getBitPSW(1);
}

bool cpu::getPSW_z() const
{
	return getBitPSW(2);
}

bool cpu::getPSW_n() const
{
	return getBitPSW(3);
}

void cpu::setBitPSW(const int bit, const bool v)
{
	psw &= ~(1 << bit);
	psw |= v << bit;
}

void cpu::setPSW_c(const bool v)
{
	setBitPSW(0, v);
}

void cpu::setPSW_v(const bool v)
{
	setBitPSW(1, v);
}

void cpu::setPSW_z(const bool v)
{
	setBitPSW(2, v);
}

void cpu::setPSW_n(const bool v)
{
	setBitPSW(3, v);
}

void cpu::setPSW_spl(const int v)
{
	psw &= ~(7 << 5);
	psw |= v << 5;
}

int cpu::getPSW_spl() const
{
	return (psw >> 5) & 7;
}

void cpu::setPSW(const uint16_t v, const bool limited)
{
	if (limited) {
		// cannot replace the run-mode bits nor the set of registers
		// psw = (psw & ~0340) | (v & 0174340);
		psw = (psw & 0177400) | (v & 037777);
	}
	else {
		psw = v;
	}
}

void cpu::setPSW_flags_nzv(const uint16_t value, const word_mode_t word_mode)
{
	setPSW_n(SIGN(value, word_mode));
	setPSW_z(IS_0(value, word_mode));
	setPSW_v(false);
}

bool cpu::check_pending_interrupts() const
{
	if (trap_delay.has_value() && trap_delay.value() > 1)
		return false;

	uint8_t start_level = getPSW_spl() + 1;

	for(uint8_t i=start_level; i < 8; i++) {
		auto interrupts = queued_interrupts.find(i);

		assert(interrupts != queued_interrupts.end());

		if (interrupts->second.empty() == false)
			return true;
	}

	return false;
}

bool cpu::execute_any_pending_interrupt()
{
#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(qi_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(qi_lock);
#endif

	bool can_trigger = false;

	if (trap_delay.has_value()) {
		trap_delay.value()--;

		TRACE("Delayed trap: %d instructions left", trap_delay.value());

		if (trap_delay.value() > 0)
			return false;

		trap_delay.reset();

		can_trigger = true;
	}

	any_queued_interrupts = false;

	uint8_t current_level = getPSW_spl();

	// uint8_t start_level = current_level <= 3 ? 0 : current_level + 1;
	// PDP-11_70_Handbook_1977-78.pdf page 1-5, "processor priority"
	uint8_t start_level   = current_level + 1;

	for(uint8_t i=0; i < 8; i++) {
		auto interrupts = queued_interrupts.find(i);

		if (interrupts->second.empty() == false) {
			any_queued_interrupts = true;

			if (i < start_level)  // at leas we know now that there's an interrupt scheduled
				continue;

			if (can_trigger == false) {
				trap_delay = initial_trap_delay;
				return false;
			}

			auto    vector = interrupts->second.begin();

			uint8_t v      = *vector;

			interrupts->second.erase(vector);

			TRACE("Invoking interrupt vector %o (IPL %d, current: %d)", v, i, current_level);

			trap(v, i, true);

			// when there are more interrupts scheduled, invoke them asap
			trap_delay = initial_trap_delay;

#if defined(BUILD_FOR_RP2040)
			xSemaphoreGive(qi_lock);
#endif

			return true;
		}
	}

	if (any_queued_interrupts && trap_delay.has_value() == false)
		trap_delay = initial_trap_delay;

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(qi_lock);
#endif

	return false;
}

void cpu::queue_interrupt(const uint8_t level, const uint8_t vector)
{
#if defined(BUILD_FOR_RP2040)
	xSemaphoreTake(qi_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(qi_lock);
#endif

	auto it = queued_interrupts.find(level);
	assert(it != queued_interrupts.end());
	it->second.insert(vector);

#if defined(BUILD_FOR_RP2040)
	xSemaphoreGive(qi_lock);

	uint8_t value = 1;
	xQueueSend(qi_q, &value, portMAX_DELAY);
#else

	qi_cv.notify_all();
#endif

	any_queued_interrupts = true;

	TRACE("Queueing interrupt vector %o (IPL %d, current: %d), n: %zu", vector, level, getPSW_spl(), it->second.size());
}

void cpu::addToMMR1(const gam_rc_t & g)
{
	if (!b->getMMU()->isMMR1Locked() && g.mmr1_update.has_value()) {
		assert(g.mmr1_update.value().delta);

		b->getMMU()->addToMMR1(g.mmr1_update.value().delta, g.mmr1_update.value().reg);
	}
}

// GAM = general addressing modes
gam_rc_t cpu::getGAM(const uint8_t mode, const uint8_t reg, const word_mode_t word_mode, const bool read_value)
{
	gam_rc_t g { word_mode, rm_cur, i_space, mode, { }, { }, { }, { } };

	d_i_space_t isR7_space = reg == 7 ? i_space : (b->getMMU()->get_use_data_space(getPSW_runmode()) ? d_space : i_space);
	//                                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ always d_space here? TODO

	g.space     = isR7_space;

	uint16_t next_word = 0;

	switch(mode) {
		case 0:  // Rn
			g.reg   = reg;
			g.value = get_register(reg) & (word_mode == wm_byte ? 0xff : 0xffff);
			break;
		case 1:  // (Rn)
			g.addr  = get_register(reg);
			if (read_value)
				g.value = b->read(g.addr.value(), word_mode, rm_cur, isR7_space);
			break;
		case 2:  // (Rn)+  /  #n
			g.addr  = get_register(reg);
			if (read_value)
				g.value = b->read(g.addr.value(), word_mode, rm_cur, isR7_space);
			add_register(reg, word_mode == wm_word || reg == 7 || reg == 6 ? 2 : 1);
			g.mmr1_update = { word_mode == wm_word || reg == 7 || reg == 6 ? 2 : 1, reg };
			break;
		case 3:  // @(Rn)+  /  @#a
			g.addr  = b->read(get_register(reg), wm_word, rm_cur, isR7_space);
			// might be wrong: the adds should happen when the read is really performed, because of traps
			add_register(reg, 2);
			g.mmr1_update = { 2, reg };
			g.space = d_space;
			if (read_value)
				g.value = b->read(g.addr.value(), word_mode, rm_cur, g.space);
			break;
		case 4:  // -(Rn)
			add_register(reg, word_mode == wm_word || reg == 7 || reg == 6 ? -2 : -1);
			g.mmr1_update = { word_mode == wm_word || reg == 7 || reg == 6 ? -2 : -1, reg };
			g.space = d_space;
			g.addr  = get_register(reg);
			if (read_value)
				g.value = b->read(g.addr.value(), word_mode, rm_cur, isR7_space);
			break;
		case 5:  // @-(Rn)
			add_register(reg, -2);
			g.mmr1_update = { -2, reg };
			g.addr  = b->read(get_register(reg), wm_word, rm_cur, isR7_space);
			g.space = d_space;
			if (read_value)
				g.value = b->read(g.addr.value(), word_mode, rm_cur, g.space);
			break;
		case 6:  // x(Rn)  /  a
			next_word = b->read(getPC(), wm_word, rm_cur, i_space);
			add_register(7, + 2);
			g.addr  = get_register(reg) + next_word;
			g.space = d_space;
			if (read_value)
				g.value = b->read(g.addr.value(), word_mode, rm_cur, g.space);
			break;
		case 7:  // @x(Rn)  /  @a
			next_word = b->read(getPC(), wm_word, rm_cur, i_space);
			add_register(7, + 2);
			g.addr  = b->read(get_register(reg) + next_word, wm_word, rm_cur, d_space);
			g.space = d_space;
			if (read_value)
				g.value = b->read(g.addr.value(), word_mode, rm_cur, g.space);
			break;
	}

	assert(g.value < 256 || word_mode == wm_word);

	return g;
}

bool cpu::putGAM(const gam_rc_t & g, const uint16_t value)
{
	assert(value < 256 || g.word_mode == wm_word);

	if (g.addr.has_value()) {
		auto rc = b->write(g.addr.value(), g.word_mode, value, g.mode_selection, g.space);

		return rc == false;
	}

	if (g.mode_selection == rm_prev) {
		assert(g.reg.value() == 6);
		sp[getPSW_prev_runmode()] = value;
	}
	else {
		set_register(g.reg.value(), value);
	}

	return true;
}

gam_rc_t cpu::getGAMAddress(const uint8_t mode, const int reg, const word_mode_t word_mode)
{
	return getGAM(mode, reg, word_mode, false);
}

bool cpu::double_operand_instructions(const uint16_t instr)
{
	const uint8_t     operation = (instr >> 12) & 7;

	if (operation == 0b000)
		return single_operand_instructions(instr);

	const word_mode_t word_mode = instr & 0x8000 ? wm_byte : wm_word;

	if (operation == 0b111) {
		if (word_mode == wm_byte)
			return false;

		return additional_double_operand_instructions(instr);
	}

	const uint8_t src        = (instr >> 6) & 63;
	const uint8_t src_mode   = (src >> 3) & 7;
	const uint8_t src_reg    = src & 7;

	const uint8_t dst        = instr & 63;
	const uint8_t dst_mode   = (dst >> 3) & 7;
	const uint8_t dst_reg    = dst & 7;

	switch(operation) {
		case 0b001: { // MOV/MOVB Move Word/Byte
				    gam_rc_t g_src = getGAM(src_mode, src_reg, word_mode);

				    bool set_flags = true;

				    if (word_mode == wm_byte && dst_mode == 0)
					    set_register(dst_reg, int8_t(g_src.value.value()));  // int8_t: sign extension
				    else {
					    auto g_dst = getGAMAddress(dst_mode, dst_reg, word_mode);
					    addToMMR1(g_dst);

					    set_flags = putGAM(g_dst, g_src.value.value());
				    }

				    addToMMR1(g_src);

				    if (set_flags)
					    setPSW_flags_nzv(g_src.value.value(), word_mode);

				    return true;
			    }

		case 0b010: { // CMP/CMPB Compare Word/Byte
				    gam_rc_t g_src = getGAM(src_mode, src_reg, word_mode);

				    auto     g_dst = getGAM(dst_mode, dst_reg, word_mode);

				    addToMMR1(g_dst);
				    addToMMR1(g_src);

				    uint16_t temp  = (g_src.value.value() - g_dst.value.value()) & (word_mode == wm_byte ? 0xff : 0xffff);

				    setPSW_n(SIGN(temp, word_mode));
				    setPSW_z(IS_0(temp, word_mode));
				    setPSW_v(SIGN((g_src.value.value() ^ g_dst.value.value()) & (~g_dst.value.value() ^ temp), word_mode));
				    setPSW_c(g_src.value.value() < g_dst.value.value());

				    return true;
			    }

		case 0b011: { // BIT/BITB Bit Test Word/Byte
				    gam_rc_t g_src  = getGAM(src_mode, src_reg, word_mode);

				    auto     g_dst  = getGAM(dst_mode, dst_reg, word_mode);

				    addToMMR1(g_dst);
				    addToMMR1(g_src);

				    uint16_t result = (g_dst.value.value() & g_src.value.value()) & (word_mode == wm_byte ? 0xff : 0xffff);

				    setPSW_flags_nzv(result, word_mode);

				    return true;
			    }

		case 0b100: { // BIC/BICB Bit Clear Word/Byte
				  gam_rc_t g_src  = getGAM(src_mode, src_reg, word_mode);

				  if (dst_mode == 0) {
					  addToMMR1(g_src);  // keep here because of order of updates

					  uint16_t v      = get_register(dst_reg);  // need the full word
					  uint16_t result = v & ~g_src.value.value();

					  set_register(dst_reg, result);

					  setPSW_flags_nzv(result, word_mode);
				  }
				  else {
					  auto     g_dst  = getGAM(dst_mode, dst_reg, word_mode);

					  addToMMR1(g_dst);
					  addToMMR1(g_src);

					  uint16_t result = g_dst.value.value() & ~g_src.value.value();

					  if (put_result(g_dst, result))
						  setPSW_flags_nzv(result, word_mode);
				  }

				  return true;
			    }

		case 0b101: { // BIS/BISB Bit Set Word/Byte
				  gam_rc_t g_src  = getGAM(src_mode, src_reg, word_mode);

				  if (dst_mode == 0) {
					  addToMMR1(g_src);  // keep here because of order of updates

					  uint16_t v      = get_register(dst_reg);  // need the full word
					  uint16_t result = v | g_src.value.value();

					  set_register(dst_reg, result);

					  setPSW_n(SIGN(result, word_mode));
					  setPSW_z(IS_0(result, word_mode));
					  setPSW_v(false);
				  }
				  else {
					  auto     g_dst  = getGAM(dst_mode, dst_reg, word_mode);

					  addToMMR1(g_dst);
					  addToMMR1(g_src);

					  uint16_t result = g_dst.value.value() | g_src.value.value();

					  if (put_result(g_dst, result)) {
						  setPSW_n(SIGN(result, word_mode));
						  setPSW_z(IS_0(result, word_mode));
						  setPSW_v(false);
					  }
				  }

				  return true;
			    }

		case 0b110: { // ADD/SUB Add/Subtract Word
				    auto     g_ssrc = getGAM(src_mode, src_reg, wm_word);

				    auto     g_dst  = getGAM(dst_mode, dst_reg, wm_word);

				    addToMMR1(g_dst);
				    addToMMR1(g_ssrc);

				    int16_t  result = 0;

				    bool     set_flags  = true;

				    if (g_dst.addr.has_value())
					set_flags = !b->is_psw(g_dst.addr.value(), g_dst.mode_selection, g_dst.space);

				    if (instr & 0x8000) {  // SUB
					    result = (g_dst.value.value() - g_ssrc.value.value()) & 0xffff;

					    if (set_flags) {
						    setPSW_v(SIGN((g_dst.value.value() ^ g_ssrc.value.value()) & (~g_ssrc.value.value() ^ result), wm_word));
						    setPSW_c(uint16_t(g_dst.value.value()) < uint16_t(g_ssrc.value.value()));
					    }
				    }
				    else {  // ADD
					    uint32_t temp = g_dst.value.value() + g_ssrc.value.value();

					    result = temp;

					    if (set_flags) {
						    setPSW_v(SIGN((~g_ssrc.value.value() ^ g_dst.value.value()) & (g_ssrc.value.value() ^ (temp & 0xffff)), wm_word));
						    setPSW_c(uint16_t(result) < uint16_t(g_ssrc.value.value()));
					    }
				    }

				    if (set_flags) {
					    setPSW_n(result < 0);
					    setPSW_z(result == 0);
				    }

				    (void)putGAM(g_dst, result);

				    return true;
			    }
	}

	return false;
}

bool cpu::additional_double_operand_instructions(const uint16_t instr)
{
	const uint8_t reg = (instr >> 6) & 7;

	const uint8_t dst = instr & 63;
	const uint8_t dst_mode = (dst >> 3) & 7;
	const uint8_t dst_reg = dst & 7;

	const int operation = (instr >> 9) & 7;

	switch(operation) {
		case 0: { // MUL
				int16_t R1  = get_register(reg);

				auto    R2g = getGAM(dst_mode, dst_reg, wm_word);
			        addToMMR1(R2g);
				int16_t R2  = R2g.value.value();

				int32_t result = R1 * R2;

				set_register(reg, result >> 16);
				set_register(reg | 1, result & 65535);

				setPSW_n(result < 0);
				setPSW_z(result == 0);
				setPSW_v(false);
				setPSW_c(result < -32768 || result > 32767);
				return true;
			}

		case 1: { // DIV
				auto    R2g     = getGAM(dst_mode, dst_reg, wm_word);
			        addToMMR1(R2g);
				int16_t divider = R2g.value.value();

				int32_t R0R1    = (uint32_t(get_register(reg)) << 16) | get_register(reg | 1);

				if (divider == 0) {  // divide by zero
					setPSW_n(false);
					setPSW_z(true);
					setPSW_v(true);
					setPSW_c(true);

					return true;
				}
				else if (divider == -1 && uint32_t(R0R1) == 0x80000000) {  // maximum negative value; too big
					setPSW_n(false);
					setPSW_z(false);
					setPSW_v(true);
					setPSW_c(false);

					return true;
				}

				int32_t quot = R0R1 / divider;
				int16_t rem  = R0R1 % divider;

				setPSW_n(quot < 0);
				setPSW_z(quot == 0);
				setPSW_c(false);

				if (quot > 32767 || quot < -32768) {
					setPSW_v(true);

					return true;
				}

				set_register(reg, quot);
				set_register(reg | 1, rem);

				setPSW_v(false);

				return true;
			}

		case 2: { // ASH
				uint32_t R     = get_register(reg), oldR = R;

			        auto     g_dst = getGAM(dst_mode, dst_reg, wm_word);
			        addToMMR1(g_dst);
				uint16_t shift = g_dst.value.value() & 077;

				TRACE("shift %06o with %d", R, shift);

				bool     sign  = SIGN(R, wm_word);

				if (shift == 0) {
					setPSW_c(false);
					setPSW_v(false);
				}
				else if (shift <= 15) {
					R <<= shift;
					setPSW_c(R & 0x10000);
					setPSW_v(SIGN(oldR, wm_word) != SIGN(R, wm_word));
				}
				else if (shift < 32) {
					setPSW_c((R << (shift - 16)) & 1);
					R = 0;
					setPSW_v(SIGN(oldR, wm_word) != SIGN(R, wm_word));
				}
				else if (shift == 32) {
					R = -sign;

					setPSW_c(sign);
					setPSW_v(SIGN(R, wm_word) != SIGN(oldR, wm_word));
				}
				else {
                                        int      shift_n     = 64 - shift;
                                        uint32_t sign_extend = sign ? 0x8000 : 0;

                                        for(int i=0; i<shift_n; i++) {
                                                setPSW_c(R & 1);
                                                R >>= 1;
                                                R |= sign_extend;
                                        }

					setPSW_v(SIGN(R, wm_word) != SIGN(oldR, wm_word));
				}

				R &= 0xffff;

				setPSW_n(SIGN(R, wm_word));
				setPSW_z(R == 0);

				set_register(reg, R);

				return true;
			}

		case 3: { // ASHC
				uint32_t R0R1  = (uint32_t(get_register(reg)) << 16) | get_register(reg | 1);

			        auto     g_dst = getGAM(dst_mode, dst_reg, wm_word);
			        addToMMR1(g_dst);
				uint16_t shift = g_dst.value.value() & 077;

				bool     sign  = R0R1 & 0x80000000;

				setPSW_v(false);

				if (shift == 0)
					setPSW_c(false);
				else if (shift < 32) {
					R0R1 <<= shift - 1;

					setPSW_c(R0R1 >> 31);

					R0R1 <<= 1;
				}
				else if (shift == 32) {
					R0R1 = -sign;

					setPSW_c(sign);
				}
				else {
					int shift_n = (64 - shift) - 1;

					// extend sign-bit
					if (sign) {  // convert to unsigned 64b int & extend sign
						R0R1 = (uint64_t(R0R1) | 0xffffffff00000000ll) >> shift_n;

						setPSW_c(R0R1 & 1);

						R0R1 = (uint64_t(R0R1) | 0xffffffff00000000ll) >> 1;
					}
					else {
						R0R1 >>= shift_n;

						setPSW_c(R0R1 & 1);

						R0R1 >>= 1;
					}
				}

				bool new_sign = R0R1 & 0x80000000;
				setPSW_v(sign != new_sign);

				set_register(reg, R0R1 >> 16);
				set_register(reg | 1, R0R1 & 65535);

				setPSW_n(R0R1 & 0x80000000);
				setPSW_z(R0R1 == 0);

				return true;
			}

		case 4: { // XOR (word only)
			  	uint16_t reg_v = get_register(reg);  // in case it is R7
			        auto     g_dst = getGAM(dst_mode, dst_reg, wm_word);
				addToMMR1(g_dst);
				uint16_t vl    = g_dst.value.value() ^ reg_v;

				bool set_flags = putGAM(g_dst, vl);

				if (set_flags)
				    setPSW_flags_nzv(vl, wm_word);

				return true;
			}

		case 7: { // SOB
				if (add_register(reg, -1)) {
					uint16_t newPC = getPC() - dst * 2;

					setPC(newPC);
				}

				return true;
			}
	}

	return false;
}

bool cpu::single_operand_instructions(const uint16_t instr)
{
	const uint16_t opcode    = (instr >> 6) & 0b111111111;
	const uint8_t  dst       = instr & 63;
	const uint8_t  dst_mode  = (dst >> 3) & 7;
	const uint8_t  dst_reg   = dst & 7;
	const word_mode_t word_mode = instr & 0x8000 ? wm_byte : wm_word;

	switch(opcode) {
		case 0b00000011: { // SWAB
					 if (word_mode == wm_byte) // handled elsewhere
						 return false;

					 auto g_dst = getGAM(dst_mode, dst_reg, word_mode);
					 addToMMR1(g_dst);

					 uint16_t v = g_dst.value.value();

					 v = (v << 8) | (v >> 8);

					 bool set_flags = putGAM(g_dst, v);

					 if (set_flags) {
						 setPSW_flags_nzv(v, wm_byte);
						 setPSW_c(false);
					 }

					 break;
				 }

		case 0b000101000: { // CLR/CLRB
					  bool set_flags = false;

					  if (word_mode == wm_byte && dst_mode == 0) {
						  uint16_t v = get_register(dst_reg) & 0xff00;

						  set_register(dst_reg, v);

						  set_flags = true;
					  }
					  else {
						  auto g_dst = getGAMAddress(dst_mode, dst_reg, word_mode);
						  addToMMR1(g_dst);

						  set_flags = putGAM(g_dst, 0);
					  }

					  if (set_flags) {
						  setPSW_n(false);
						  setPSW_z(true);
						  setPSW_v(false);
						  setPSW_c(false);
					  }

					  break;
				  }

		case 0b000101001: { // COM/COMB
					  bool     set_flags = false;
					  uint16_t v         = 0;

					  if (word_mode == wm_byte && dst_mode == 0) {
						  v = get_register(dst_reg) ^ 0xff;

						  set_register(dst_reg, v);

						  set_flags = true;
					  }
					  else {
						  auto a = getGAM(dst_mode, dst_reg, word_mode);
						  addToMMR1(a);
						  v = a.value.value();

						  if (word_mode == wm_byte)
							  v ^= 0xff;
						  else
							  v ^= 0xffff;

						  set_flags = putGAM(a, v);
					  }

					  if (set_flags) {
						  setPSW_flags_nzv(v, word_mode);
						  setPSW_c(true);
					  }

					  break;
				  }

		case 0b000101010: { // INC/INCB
					  if (dst_mode == 0) {
						  uint16_t v   = get_register(dst_reg);
						  uint16_t add = word_mode == wm_byte ? v & 0xff00 : 0;

						  v = (v + 1) & (word_mode == wm_byte ? 0xff : 0xffff);
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(word_mode == wm_byte ? (v & 0xff) == 0x80 : v == 0x8000);

						  set_register(dst_reg, v);
					  }
					  else {
						  auto    a         = getGAM(dst_mode, dst_reg, word_mode);
						  addToMMR1(a);
						  int32_t vl        = (a.value.value() + 1) & (word_mode == wm_byte ? 0xff : 0xffff);

						  bool    set_flags = b->write(a.addr.value(), a.word_mode, vl, a.mode_selection, a.space) == false;

						  if (set_flags) {
							  setPSW_n(SIGN(vl, word_mode));
							  setPSW_z(IS_0(vl, word_mode));
							  setPSW_v(word_mode == wm_byte ? vl == 0x80 : vl == 0x8000);
						  }
					  }

					  break;
				  }

		case 0b000101011: { // DEC/DECB
					  // TODO unify
					  if (dst_mode == 0) {
						  uint16_t v   = get_register(dst_reg);
						  uint16_t add = word_mode == wm_byte ? v & 0xff00 : 0;

						  v = (v - 1) & (word_mode == wm_byte ? 0xff : 0xffff);
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(word_mode == wm_byte ? (v & 0xff) == 0x7f : v == 0x7fff);

						  set_register(dst_reg, v);
					  }
					  else {
						  auto     a         = getGAM(dst_mode, dst_reg, word_mode);
						  addToMMR1(a);
						  int32_t  vl        = (a.value.value() - 1) & (word_mode == wm_byte ? 0xff : 0xffff);

						  bool     set_flags = b->write(a.addr.value(), a.word_mode, vl, a.mode_selection, a.space) == false;

						  if (set_flags) {
							  setPSW_n(SIGN(vl, word_mode));
							  setPSW_z(IS_0(vl, word_mode));
							  setPSW_v(word_mode == wm_byte ? vl == 0x7f : vl == 0x7fff);
						  }
					  }

					  break;
				  }

		case 0b000101100: { // NEG/NEGB
					  if (dst_mode == 0) {
						  uint16_t v   = get_register(dst_reg);
						  uint16_t add = word_mode == wm_byte ? v & 0xff00 : 0;

						  v = (-v) & (word_mode == wm_byte ? 0xff : 0xffff);
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(word_mode == wm_byte ? (v & 0xff) == 0x80 : v == 0x8000);
						  setPSW_c(v);

						  set_register(dst_reg, v);
					  }
					  else {
						  auto     a = getGAM(dst_mode, dst_reg, word_mode);
						  addToMMR1(a);
						  uint16_t v = -a.value.value();

						  bool set_flags = b->write(a.addr.value(), a.word_mode, v, a.mode_selection, a.space) == false;

						  if (set_flags) {
							  setPSW_n(SIGN(v, word_mode));
							  setPSW_z(IS_0(v, word_mode));
							  setPSW_v(word_mode == wm_byte ? (v & 0xff) == 0x80 : v == 0x8000);
							  setPSW_c(v);
						  }
					  }

					  break;
				  }

		case 0b000101101: { // ADC/ADCB
					  if (dst_mode == 0) {
						  const uint16_t vo    = get_register(dst_reg);
						  uint16_t       v     = vo;
						  uint16_t       add   = word_mode == wm_byte ? v & 0xff00 : 0;
						  bool           org_c = getPSW_c();

						  v = (v + org_c) & (word_mode == wm_byte ? 0xff : 0xffff);
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v((word_mode == wm_byte ? (vo & 0xff) == 0x7f : vo == 0x7fff) && org_c);
						  setPSW_c((word_mode == wm_byte ? (vo & 0xff) == 0xff : vo == 0xffff) && org_c);

						  set_register(dst_reg, v);
					  }
					  else {
						  auto           a     = getGAM(dst_mode, dst_reg, word_mode);
						  addToMMR1(a);
						  const uint16_t vo    = a.value.value();
						  bool           org_c = getPSW_c();
						  uint16_t       v     = (vo + org_c) & (word_mode == wm_byte ? 0x00ff : 0xffff);

						  bool set_flags = b->write(a.addr.value(), a.word_mode, v, a.mode_selection, a.space) == false;

						  if (set_flags) {
							  setPSW_n(SIGN(v, word_mode));
							  setPSW_z(IS_0(v, word_mode));
							  setPSW_v((word_mode == wm_byte ? (vo & 0xff) == 0x7f : vo == 0x7fff) && org_c);
							  setPSW_c((word_mode == wm_byte ? (vo & 0xff) == 0xff : vo == 0xffff) && org_c);
						  }
					  }

					  break;
				  }

		case 0b000101110: { // SBC/SBCB
					  if (dst_mode == 0) {
						  uint16_t       v     = get_register(dst_reg);
						  const uint16_t vo    = v;
						  uint16_t       add   = word_mode == wm_byte ? v & 0xff00 : 0;
						  bool           org_c = getPSW_c();

						  v = (v - org_c) & (word_mode == wm_byte ? 0xff : 0xffff);
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v((word_mode == wm_byte ? (vo & 0xff) == 0x80 : vo == 0x8000) && org_c);
						  setPSW_c(IS_0(vo, word_mode) && org_c);

						  set_register(dst_reg, v);
					  }
					  else {
						  auto           a     = getGAM(dst_mode, dst_reg, word_mode);
						  addToMMR1(a);
						  const uint16_t vo    = a.value.value();
						  bool           org_c = getPSW_c();
						  uint16_t       v     = (vo - org_c) & (word_mode == wm_byte ? 0xff : 0xffff);

						  bool set_flags = b->write(a.addr.value(), a.word_mode, v, a.mode_selection, a.space) == false;

						  if (set_flags) {
							  setPSW_n(SIGN(v, word_mode));
							  setPSW_z(IS_0(v, word_mode));
							  setPSW_v((word_mode == wm_byte ? (vo & 0xff) == 0x80 : vo == 0x8000) && org_c);
							  setPSW_c(IS_0(vo, word_mode) && org_c);
						  }
					  }
					  break;
				  }

		case 0b000101111: { // TST/TSTB
				    	  auto     g = getGAM(dst_mode, dst_reg, word_mode);
					  uint16_t v = g.value.value();
					  addToMMR1(g);

					  setPSW_flags_nzv(v, word_mode);
					  setPSW_c(false);

					  break;
				  }

		case 0b000110000: { // ROR/RORB
					  if (dst_mode == 0) {
						  uint16_t v         = get_register(dst_reg);
						  bool     new_carry = v & 1;

						  uint16_t temp = 0;
						  if (word_mode == wm_byte)
							  temp = (((v & 0xff) >> 1) | (getPSW_c() << 7)) | (v & 0xff00);
						  else
							  temp = (v >> 1) | (getPSW_c() << 15);

						  set_register(dst_reg, temp);

						  setPSW_c(new_carry);
						  setPSW_n(SIGN(temp, word_mode));
						  setPSW_z(IS_0(temp, word_mode));
						  setPSW_v(getPSW_c() ^ getPSW_n());
					  }
					  else {
						  auto     a         = getGAM(dst_mode, dst_reg, word_mode);
					          addToMMR1(a);
						  uint16_t t         = a.value.value();
						  bool     new_carry = t & 1;

						  uint16_t temp = 0;
						  if (word_mode == wm_byte)
							  temp = (t >> 1) | (getPSW_c() <<  7);
						  else
							  temp = (t >> 1) | (getPSW_c() << 15);

						  bool set_flags = b->write(a.addr.value(), a.word_mode, temp, a.mode_selection, a.space) == false;

						  if (set_flags) {
							  setPSW_c(new_carry);
							  setPSW_n(SIGN(temp, word_mode));
							  setPSW_z(IS_0(temp, word_mode));
							  setPSW_v(getPSW_c() ^ getPSW_n());
						  }
					  }
					  break;
				  }

		case 0b000110001: { // ROL/ROLB
					  if (dst_mode == 0) {
						  uint16_t v         = get_register(dst_reg);
						  bool     new_carry = false;

						  uint16_t temp = 0;
						  if (word_mode == wm_byte) {
							  new_carry = v & 0x80;
							  temp = (((v << 1) | getPSW_c()) & 0xff) | (v & 0xff00);
						  }
						  else {
							  new_carry = v & 0x8000;
							  temp = (v << 1) | getPSW_c();
						  }

						  set_register(dst_reg, temp);

						  setPSW_c(new_carry);
						  setPSW_n(SIGN(temp, word_mode));
						  setPSW_z(IS_0(temp, word_mode));
						  setPSW_v(getPSW_c() ^ getPSW_n());
					  }
					  else {
						  auto     a         = getGAM(dst_mode, dst_reg, word_mode);
					          addToMMR1(a);
						  uint16_t t         = a.value.value();
						  bool     new_carry = false;

						  uint16_t temp = 0;
						  if (word_mode == wm_byte) {
							  new_carry = t & 0x80;
							  temp = ((t << 1) | getPSW_c()) & 0xff;
						  }
						  else {
							  new_carry = t & 0x8000;
							  temp = (t << 1) | getPSW_c();
						  }

						  bool set_flags = b->write(a.addr.value(), a.word_mode, temp, a.mode_selection, a.space) == false;

						  if (set_flags) {
							  setPSW_c(new_carry);
							  setPSW_n(SIGN(temp, word_mode));
							  setPSW_z(IS_0(temp, word_mode));
							  setPSW_v(getPSW_c() ^ getPSW_n());
						  }
					  }
					  break;
				  }

		case 0b000110010: { // ASR/ASRB
					  if (dst_mode == 0) {
						  uint16_t v  = get_register(dst_reg);
						  uint16_t hb = word_mode == wm_byte ? v & 128 : v & 32768;

						  setPSW_c(v & 1);

						  if (word_mode == wm_byte)
							  v = ((v & 255) >> 1) | (v & 0xff00);
						  else
							  v >>= 1;
						  v |= hb;

						  set_register(dst_reg, v);

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(getPSW_n() ^ getPSW_c());
					  }
					  else {
						  auto     a   = getGAM(dst_mode, dst_reg, word_mode);
					          addToMMR1(a);
						  uint16_t v   = a.value.value();

						  uint16_t hb  = word_mode == wm_byte ? v & 128 : v & 32768;

						  setPSW_c(v & 1);

						  if (word_mode == wm_byte)
							  v = ((v & 255) >> 1) | (v & 0xff00);
						  else
							  v >>= 1;
						  v |= hb;

						  bool set_flags = b->write(a.addr.value(), a.word_mode, v, a.mode_selection, a.space) == false;

						  if (set_flags) {
							  setPSW_n(SIGN(v, word_mode));
							  setPSW_z(IS_0(v, word_mode));
							  setPSW_v(getPSW_n() ^ getPSW_c());
						  }
					  }
					  break;
				  }

		case 0b00110011: { // ASL/ASLB
					 if (dst_mode == 0) {
						 uint16_t vl  = get_register(dst_reg);
						 uint16_t v   = ((vl << 1) & (word_mode == wm_byte ? 0xff : 0xffff));

						 if (word_mode == wm_byte)
							 v |= vl & 0xff00;

						 setPSW_n(SIGN(v, word_mode));
						 setPSW_z(IS_0(v, word_mode));
						 setPSW_c(SIGN(vl, word_mode));
						 setPSW_v(getPSW_n() ^ getPSW_c());

						 set_register(dst_reg, v);
					 }
					 else {
						 auto     a   = getGAM(dst_mode, dst_reg, word_mode);
					         addToMMR1(a);
						 uint16_t vl  = a.value.value();
						 uint16_t v   = (vl << 1) & (word_mode == wm_byte ? 0xff : 0xffff);

						 bool set_flags = b->write(a.addr.value(), a.word_mode, v, a.mode_selection, a.space) == false;

						 if (set_flags) {
							 setPSW_n(SIGN(v, word_mode));
							 setPSW_z(IS_0(v, word_mode));
							 setPSW_c(SIGN(vl, word_mode));
							 setPSW_v(getPSW_n() ^ getPSW_c());
						 }
					 }
					 break;
				 }

		case 0b00110101: { // MFPD/MFPI
					 // always words: word_mode-bit is to select between MFPI and MFPD
					 uint16_t v = 0xffff;

					 if (dst_mode == 0) {
						 if (dst_reg == 6)
							v = sp[getPSW_prev_runmode()];
						 else
							v = get_register(dst_reg);
					 }
					 else {
						 // calculate address in current address space
						auto a = getGAMAddress(dst_mode, dst_reg, wm_word);
				                addToMMR1(a);

						// read from previous space
						v = b->read(a.addr.value(), wm_word, rm_prev, word_mode == wm_byte ? d_space : i_space);
					 }

					 setPSW_flags_nzv(v, wm_word);

					 // put on current stack
					 pushStack(v);
					 break;
				 }

		case 0b00110110: { // MTPI/MTPD
					 // always words: word_mode-bit is to select between MTPI and MTPD

					 // retrieve word from '15/14'-stack
					 uint16_t v             = popStack();
					 bool     set_flags     = true;

					 if (dst_mode == 0) {
						if (dst_reg == 6)
							sp[getPSW_prev_runmode()] = v;
						else
							set_register(dst_reg, v);
					 }
					 else {
						auto a = getGAMAddress(dst_mode, dst_reg, wm_word);
						addToMMR1(a);

						b->getMMU()->mmudebug(a.addr.value());

						a.mode_selection = rm_prev;
						a.space          = word_mode == wm_byte ? d_space : i_space;
						set_flags        = putGAM(a, v);
					 }

					 if (set_flags)
						 setPSW_flags_nzv(v, wm_word);

					 break;
				 }

		case 0b000110100: // MARK/MTPS (put something in PSW)
				 if (word_mode == wm_byte) {  // MTPS
#if 0  // not in the PDP-11/70
					 psw &= 0xff00;  // only alter lower 8 bits
					 psw |= getGAM(dst_mode, dst_reg, word_mode).value.value() & 0xef;  // can't change bit 4
#else
					 trap(010);
#endif
				 }
				 else {
					 set_register(6, getPC() + dst * 2);

					 setPC(get_register(5));

					 set_register(5, popStack());
				 }
				 break;

		case 0b000110111: {  // MFPS (get PSW to something) / SXT
				 if (word_mode == wm_byte) {  // MFPS
#if 0  // not in the PDP-11/70
					 auto g_dst = getGAM(dst_mode, dst_reg, word_mode);

					 uint16_t temp      = psw & 0xff;
					 bool     extend_b7 = psw & 128;

					 if (extend_b7 && dst_mode == 0)
						 temp |= 0xff00;

					 bool set_flags = putGAM(g_dst, temp);

					 if (set_flags) {
						 setPSW_z(temp == 0);
						 setPSW_v(false);
						 setPSW_n(extend_b7);
					 }
#else
					 trap(010);
#endif
				 }
				 else {  // SXT
					 auto     g_dst = getGAM(dst_mode, dst_reg, word_mode);
					 addToMMR1(g_dst);

					 uint16_t vl    = -getPSW_n();

					 if (put_result(g_dst, vl)) {
						 setPSW_z(getPSW_n() == false);
						 setPSW_v(false);
					 }
				 }
			 }

			 break;

		default:
				 return false;
	}

	return true;
}

bool cpu::conditional_branch_instructions(const uint16_t instr)
{
	const uint8_t opcode = instr >> 8;
	const int8_t  offset = instr;
	bool          take   = false;

	switch(opcode) {
		case 0b00000001: // BR
			take = true;
			break;

		case 0b00000010: // BNE
			take = !getPSW_z();
			break;

		case 0b00000011: // BEQ
			take = getPSW_z();
			break;

		case 0b00000100: // BGE
			take = getPSW_n() == getPSW_v();
			break;

		case 0b00000101: // BLT
			take = getPSW_n() ^ getPSW_v();
			break;

		case 0b00000110: // BGT
			take = getPSW_n() == getPSW_v() && getPSW_z() == false;
			break;

		case 0b00000111: // BLE
			take = getPSW_n() != getPSW_v() || getPSW_z();
			break;

		case 0b10000000: // BPL
			take = getPSW_n() == false;
			break;

		case 0b10000001: // BMI
			take = getPSW_n() == true;
			break;

		case 0b10000010: // BHI
			take = getPSW_c() == false && getPSW_z() == false;
			break;

		case 0b10000011: // BLOS
			take = getPSW_c() || getPSW_z();
			break;

		case 0b10000100: // BVC
			take = getPSW_v() == false;
			break;

		case 0b10000101: // BVS
			take = getPSW_v();
			break;

		case 0b10000110: // BCC
			take = getPSW_c() == false;
			break;

		case 0b10000111: // BCS / BLO
			take = getPSW_c();
			break;

		default:
			return false;
	}

	if (take)
		add_register(7, offset * 2);

	return true;
}

bool cpu::condition_code_operations(const uint16_t instr)
{
	switch(instr) {
		case 0b0000000010100000: // NOP
		case 0b0000000010110000: // NOP
			return true;
	}

	if ((instr & ~7) == 0000230) { // SPLx
		if (getPSW_runmode() == 0) {  // only in kernel mode
			int level = instr & 7;

			setPSW_spl(level);
		}

//		// trap via vector 010  only(?) on an 11/60 and not(?) on an 11/70
//		trap(010);

		return true;
	}

	if ((instr & ~31) == 0b10100000) { // set condition bits
		bool state = !!(instr & 0b10000);

		if (instr & 0b1000)
			setPSW_n(state);
		if (instr & 0b0100)
			setPSW_z(state);
		if (instr & 0b0010)
			setPSW_v(state);
		if (instr & 0b0001)
			setPSW_c(state);

		return true;
	}

	return false;
}

void cpu::pushStack(const uint16_t v)
{
	if (get_register(6) == stackLimitRegister) {
		TRACE("stackLimitRegister reached %06o while pushing %06o", stackLimitRegister, v);

		trap(04, 7);
	}
	else {
		uint16_t a = add_register(6, -2);

		b->write_word(a, v, d_space);
	}
}

uint16_t cpu::popStack()
{
	uint16_t a    = get_register(6);
	uint16_t temp = b->read_word(a, d_space);

	add_register(6, 2);

	return temp;
}

bool cpu::misc_operations(const uint16_t instr)
{
	switch(instr) {
		case 0b0000000000000000: // HALT
			if (getPSW_runmode() == 0)  // only in kernel mode
				*event = EVENT_HALT;
			else
				trap(4);
			return true;

		case 0b0000000000000001: // WAIT
			{
				uint64_t start = get_us();

#if defined(BUILD_FOR_RP2040)
				uint8_t rc = 0;
				xQueueReceive(qi_q, &rc, 0);
#else
				std::unique_lock<std::mutex> lck(qi_lock);

				while (check_pending_interrupts() == false)
					qi_cv.wait(lck);
#endif
				uint64_t end = get_us();

				wait_time += end - start;  // used for MIPS calculation
			}

			TRACE("WAIT returned");

			return true;

		case 0b0000000000000010: // RTI
			if (debug_mode)
				pop_from_stack_trace();
			setPC(popStack());
			setPSW(popStack(), !!getPSW_runmode());
			psw &= ~020;  // disable TRAP flag
			return true;

		case 0b0000000000000011: // BPT
			trap(014);
			return true;

		case 0b0000000000000100: // IOT
			trap(020);
			return true;

		case 0b0000000000000110: // RTT
			if (debug_mode)
				pop_from_stack_trace();
			setPC(popStack());
			setPSW(popStack(), !!getPSW_runmode());
			return true;

		case 0b0000000000000111: // MFPT
			//set_register(0, 0);
			trap(010); // does not exist on PDP-11/70
			return true;

		case 0b0000000000000101: // RESET
			if (getPSW_runmode() == 0) {  // only in kernel mode
				b->init();

				init_interrupt_queue();
			}
			return true;
	}

	if ((instr >> 8) == 0b10001000) { // EMT
		trap(030);
		return true;
	}

	if ((instr >> 8) == 0b10001001) { // TRAP
		trap(034);
		return true;
	}

	if ((instr & ~0b111111) == 0b0000000001000000) { // JMP
		int dst_mode = (instr >> 3) & 7;
		if (dst_mode == 0)  // cannot jump to a register
			return false;

		int dst_reg = instr & 7;

		auto g = getGAMAddress(dst_mode, dst_reg, wm_word);
		addToMMR1(g);
		setPC(g.addr.value());

		return true;
	}

	if ((instr & 0b1111111000000000) == 0b0000100000000000) { // JSR
		if (debug_mode)
			add_to_stack_trace(instruction_start);

		int dst_mode = (instr >> 3) & 7;
		if (dst_mode == 0)  // cannot jump to a register
			return false;

		int  dst_reg   = instr & 7;

		auto a         = getGAMAddress(dst_mode, dst_reg, wm_word);
		auto dst_value = a.addr.value();

		int  link_reg  = (instr >> 6) & 7;

		// PUSH link
		pushStack(get_register(link_reg));
		if (!b->getMMU()->isMMR1Locked()) {
			b->getMMU()->addToMMR1(-2, 6);

			addToMMR1(a);
		}

		// MOVE PC,link
		set_register(link_reg, getPC());

		// JMP dst
		setPC(dst_value);

		return true;
	}

	if ((instr & 0b1111111111111000) == 0b0000000010000000) { // RTS
		if (debug_mode)
			pop_from_stack_trace();

		const int link_reg = instr & 7;

		// MOVE link, PC
		setPC(get_register(link_reg));

		// POP link
		uint16_t word_on_stack = b->read_word(get_register(6), d_space);

		set_register(link_reg, word_on_stack);

		// do not overwrite SP when it was just set
		if (link_reg != 6)
			add_register(6, 2);

		return true;
	}

	return false;
}

// 'is_interrupt' is not correct naming; it is true for mmu faults and interrupts
void cpu::trap(uint16_t vector, const int new_ipl, const bool is_interrupt)
{
	TRACE("*** CPU::TRAP %o, new-ipl: %d, is-interrupt: %d, run mode: %d ***", vector, new_ipl, is_interrupt, getPSW_runmode());

	uint16_t before_psw = 0;
	uint16_t before_pc  = 0;

	it_is_a_trap = true;

	do {
		try {
			processing_trap_depth++;

			bool kernel_mode = !(psw >> 14);

			if (processing_trap_depth >= 2) {
				TRACE("Trap depth %d", processing_trap_depth);

				if (processing_trap_depth >= 3) {
					*event = EVENT_HALT;
					break;
				}

				if (kernel_mode)
					vector = 4;

				set_register(6, 04);
			}
			else {
				b->getMMU()->clearMMR1();

				before_psw = getPSW();

				before_pc  = getPC();

				// TODO set MMR2?
			}

			if (debug_mode)
				add_to_stack_trace(instruction_start);

			// make sure the trap vector is retrieved from kernel space
			psw &= 037777;  // mask off 14/15 to make it into kernel-space

			setPC(b->read_word(vector + 0, d_space));

			// switch to kernel mode & update 'previous mode'
			uint16_t new_psw = b->read_word(vector + 2, d_space) & 0147777;  // mask off old 'previous mode'
			if (new_ipl != -1)
				new_psw = (new_psw & ~0xe0) | (new_ipl << 5);
			new_psw |= (before_psw >> 2) & 030000; // apply new 'previous mode'
			setPSW(new_psw, false);

			if (processing_trap_depth >= 2 && kernel_mode)
				set_register(6, 04);

			uint16_t prev_sp = get_register(6);
			try {
				pushStack(before_psw);
				pushStack(before_pc);
			}
			catch(const int exception) {
				// recover stack
				set_register(6, prev_sp);
			}

			processing_trap_depth = 0;

			// if we reach this point then the trap was processed without causing
			// another trap
			TRACE("Trapping to %06o with PSW %06o", pc, psw);
		}
		catch(const int exception) {
			TRACE("trap during execution of trap (%d)", exception);

			setPSW(before_psw, false);
		}
	}
	while(0);
}

std::optional<cpu::operand_parameters> cpu::addressing_to_string(const uint8_t mode_register, const uint16_t pc, const word_mode_t word_mode) const
{
	assert(mode_register < 64);

	int         run_mode  = getPSW_runmode();
	auto        temp      = b->peek_word(run_mode, pc & 65535);
	if (temp.has_value() == false)
		return { };
	uint16_t    next_word = temp.value();
	int         reg       = mode_register & 7;
	uint16_t    mask      = word_mode == wm_byte ? 0xff : 0xffff;

	std::optional<uint16_t> temp2;

	std::string reg_name;
	if (reg == 6)
		reg_name = "SP";
	else if (reg == 7)
		reg_name = "PC";
	else
		reg_name = format("R%d", reg);

	switch(mode_register >> 3) {
		case 0:
			return { { reg_name, 2, -1, uint16_t(get_register(reg) & mask) } };

		case 1:
			temp2 = b->peek_word(run_mode, get_register(reg));
			if (temp2.has_value() == false)
				return { };

			return { { format("(%s)", reg_name.c_str()), 2, -1, uint16_t(temp2.value() & mask) } };

		case 2:
			if (reg == 7)
				return { { format("#%06o", next_word), 4, int(next_word), uint16_t(next_word & mask) } };

			temp2 = b->peek_word(run_mode, get_register(reg));
			if (temp2.has_value() == false)
				return { };

			return { { format("(%s)+", reg_name.c_str()), 2, -1, uint16_t(temp2.value() & mask) } };

		case 3:
			if (reg == 7) {
				temp2 = b->peek_word(run_mode, next_word);
				if (temp2.has_value() == false)
					return { };

				return { { format("@#%06o", next_word), 4, int(next_word), uint16_t(temp2.value() & mask) } };
			}

			temp2 = b->peek_word(run_mode, get_register(reg));
			if (temp2.has_value() == false)
				return { };

			temp2 = b->peek_word(run_mode, temp2.value());
			if (temp2.has_value() == false)
				return { };

			return { { format("@(%s)+", reg_name.c_str()), 2, -1, uint16_t(temp2.value() & mask) } };

		case 4:
			temp2 = b->peek_word(run_mode, get_register(reg) - (word_mode == wm_word || reg >= 6 ? 2 : 1));
			if (temp2.has_value() == false)
				return { };

			return { { format("-(%s)", reg_name.c_str()), 2, -1, uint16_t(temp2.value() & mask) } };

		case 5:
			temp2 = b->peek_word(run_mode, get_register(reg) - 2);
			if (temp2.has_value() == false)
				return { };

			temp2 = b->peek_word(run_mode, temp2.value());
			if (temp2.has_value() == false)
				return { };

			return { { format("@-(%s)", reg_name.c_str()), 2, -1, uint16_t(temp2.value() & mask) } };

		case 6:
			if (reg == 7) {
				temp2 = b->peek_word(run_mode, get_register(reg) + next_word);
				if (temp2.has_value() == false)
					return { };

				return { { format("%06o", (pc + next_word + 2) & 65535), 4, int(next_word), uint16_t(temp2.value() & mask) } };
			}

			temp2 = b->peek_word(run_mode, get_register(reg) + next_word);
			if (temp2.has_value() == false)
				return { };

			return { { format("%o(%s)", next_word, reg_name.c_str()), 4, int(next_word), uint16_t(temp2.value() & mask) } };

		case 7:
			if (reg == 7) {
				temp2 = b->peek_word(run_mode, get_register(reg) + next_word);
				if (temp2.has_value() == false)
					return { };

				temp2 = b->peek_word(run_mode, temp2.value());
				if (temp2.has_value() == false)
					return { };

				return { { format("@%06o", next_word), 4, int(next_word), uint16_t(temp2.value() & mask) } };
			}

			temp2 = b->peek_word(run_mode, get_register(reg) + next_word);
			if (temp2.has_value() == false)
				return { };

			temp2 = b->peek_word(run_mode, temp2.value());
			if (temp2.has_value() == false)
				return { };

			return { { format("@%o(%s)", next_word, reg_name.c_str()), 4, int(next_word), uint16_t(temp2.value() & mask) } };
	}

	return { };
}

std::map<std::string, std::vector<std::string> > cpu::disassemble(const uint16_t addr) const
{
	auto        temp          = b->peek_word(getPSW_runmode(), addr);
	if (temp.has_value() == false)
		return { };

	uint16_t    instruction   = temp.value();
	word_mode_t word_mode     = instruction & 0x8000 ? wm_byte : wm_word;
	std::string word_mode_str = word_mode == wm_byte ? "B" : "";
	uint8_t     ado_opcode    = (instruction >>  9) &  7;  // additional double operand
        uint8_t     do_opcode     = (instruction >> 12) &  7;  // double operand
	uint8_t     so_opcode     = (instruction >>  6) & 63;  // single operand

	std::string text;
	std::string name;

	std::string space = " ";
	std::string comma = ",";

	uint8_t     src_register  = (instruction >> 6) & 63;
	uint8_t     dst_register  = (instruction >> 0) & 63;

	std::vector<uint16_t> instruction_words { instruction };
	std::vector<uint16_t> work_values;

	// TODO: 100000011

	if (do_opcode == 0b000) {
		auto addressing = addressing_to_string(dst_register, (addr + 2) & 65535, word_mode);
		if (addressing.has_value() == false)
			return { };
		auto dst_text { addressing.value() };

		auto next_word = dst_text.instruction_part;

		work_values.push_back(dst_text.work_value);

		// single_operand_instructions
		switch(so_opcode) {
			case 0b00000011:
				if (word_mode == wm_word)
					text = "SWAB " + dst_text.operand;
				break;

			case 0b000101000:
				name = "CLR";
				break;

			case 0b000101001:
				name = "COM";
				break;

			case 0b000101010:
				name = "INC";
				break;

			case 0b000101011:
				name = "DEC";
				break;

			case 0b000101100:
				name = "NEG";
				break;

			case 0b000101101:
				name = "ADC";
				break;

			case 0b000101110:
				name = "SBC";
				break;

			case 0b000101111:
				name = "TST";
				break;

			case 0b000110000:
				name = "ROR";
				break;

			case 0b000110001:
				name = "ROL";
				break;

			case 0b000110010:
				name = "ASR";
				break;

			case 0b00110011:
				name = "ASL";
				break;

			case 0b00110101:
				name = word_mode == wm_byte ? "MFPD" : "MFPI";
				break;

			case 0b00110110:
				name = word_mode == wm_byte ? "MTPD" : "MTPI";
				break;

			case 0b000110100:
				if (word_mode == wm_byte)
					name = "MTPS";
				break;

			case 0b000110111:
				if (word_mode == wm_byte)
					name = "MFPS";
				else
					name = "SXT";
				break;
		}

		if (text.empty() && name.empty() == false)
			text = name + word_mode_str + space + dst_text.operand;

		if (text.empty() == false && next_word != -1)
			instruction_words.push_back(next_word);
	}
	else if (do_opcode == 0b111) {
		if (word_mode == wm_byte)
			name = "?";
		else {
			std::string src_text = format("R%d", (instruction >> 6) & 7);
			auto        addressing = addressing_to_string(dst_register, (addr + 2) & 65535, word_mode);
			if (addressing.has_value() == false)
				return { };
			auto        dst_text { addressing.value() };

			auto next_word = dst_text.instruction_part;

			work_values.push_back(dst_text.work_value);

			switch(ado_opcode) {  // additional double operand
				case 0:
					name = "MUL";
					break;

				case 1:
					name = "DIV";
					break;

				case 2:
					name = "ASH";
					break;

				case 3:
					name = "ASHC";
					break;

				case 4:
					name = "XOR";
					break;

				case 7:
					text = std::string("SOB ") + src_text;
					break;
			}

			if (text.empty() && name.empty() == false)
				text = name + space + src_text + comma + dst_text.operand;  // TODO: swap for ASH, ASHC

			if (text.empty() == false && next_word != -1)
				instruction_words.push_back(next_word);
		}
	}
	else {
		switch(do_opcode) {
			case 0b001:
				name = "MOV";
				break;

			case 0b010:
				name = "CMP";
				break;

			case 0b011:
				name = "BIT";
				break;

			case 0b100:
				name = "BIC";
				break;

			case 0b101:
				name = "BIS";
				break;

			case 0b110:
				if (word_mode == wm_byte)
					name = "SUB";
				else
					name = "ADD";
				break;
		}

		// source
		auto addressing_src = addressing_to_string(src_register, (addr + 2) & 65535, word_mode);
		if (addressing_src.has_value() == false)
			return { };
		auto src_text { addressing_src.value() };

		auto next_word_src = src_text.instruction_part;
		if (next_word_src != -1)
			instruction_words.push_back(next_word_src);

		work_values.push_back(src_text.work_value);

		// destination
		auto addressing_dst = addressing_to_string(dst_register, (addr + src_text.length) & 65535, word_mode);
		if (addressing_dst.has_value() == false)
			return { };
		auto dst_text { addressing_dst.value() };

		auto next_word_dst = dst_text.instruction_part;
		if (next_word_dst != -1)
			instruction_words.push_back(next_word_dst);

		work_values.push_back(dst_text.work_value);

		text = name + word_mode_str + space + src_text.operand + comma + dst_text.operand;
	}

	if (text.empty()) {  // conditional branch instructions
		uint8_t  cb_opcode = (instruction >> 8) & 255;
		int8_t   offset    = instruction & 255;
		uint16_t new_pc    = (addr + 2 + offset * 2) & 65535;

		switch(cb_opcode) {
			case 0b00000001:
				name = "BR";
				break;

			case 0b00000010:
				name = "BNE";
				break;

			case 0b00000011:
				name = "BEQ";
				break;

			case 0b00000100:
				name = "BGE";
				break;

			case 0b00000101:
				name = "BLT";
				break;

			case 0b00000110:
				name = "BGT";
				break;

			case 0b00000111:
				name = "BLE";
				break;

			case 0b10000000:
				name = "BPL";
				break;

			case 0b10000001:
				name = "BMI";
				break;

			case 0b10000010:
				name = "BHI";
				break;

			case 0b10000011:
				name = "BLOS";
				break;

			case 0b10000100:
				name = "BVC";
				break;

			case 0b10000101:
				name = "BVS";
				break;

			case 0b10000110:
				name = "BCC";
				break;

			case 0b10000111:
				name = "BCS/BLO";
				break;
		}

		if (text.empty() && name.empty() == false)
			text = name + space + format("%06o", new_pc);
	}

	if (text.empty()) {
		if ((instruction & ~7) == 0000230)
			text = format("SPL%d", instruction & 7);

		if ((instruction & ~31) == 0b10100000) { // set condition bits
			text = instruction & 0b10000 ? "SE" : "CL";

			if (instruction & 0b1000)
				text += "N";
			if (instruction & 0b0100)
				text += "Z";
			if (instruction & 0b0010)
				text += "V";
			if (instruction & 0b0001)
				text += "C";
		}

		switch(instruction) {
			case 0b0000000010100000:
			case 0b0000000010110000:
				text = "NOP";
				work_values.clear();
				break;

			case 0b0000000000000000:
				text = "HALT";
				work_values.clear();
				break;

			case 0b0000000000000001:
				text = "WAIT";
				work_values.clear();
				break;

			case 0b0000000000000010:
				text = "RTI";
				work_values.clear();
				break;

			case 0b0000000000000011:
				text = "BPT";
				break;

			case 0b0000000000000100:
				text = "IOT";
				break;

			case 0b0000000000000110:
				text = "RTT";
				work_values.clear();
				break;

			case 0b0000000000000111:
				text = "MFPT";
				break;

			case 0b0000000000000101:
				text = "RESET";
				work_values.clear();
				break;
		}

		if ((instruction >> 8) == 0b10001000)
			text = format("EMT %o", instruction & 255);

		if ((instruction >> 8) == 0b10001001)
			text = format("TRAP %o", instruction & 255);

		if ((instruction & ~0b111111) == 0b0000000001000000) {
			auto addressing = addressing_to_string(dst_register, (addr + 2) & 65535, word_mode);
			if (addressing.has_value() == false)
				return { };
			auto dst_text { addressing.value() };

			auto next_word = dst_text.instruction_part;
			if (next_word != -1)
				instruction_words.push_back(next_word);

			work_values.push_back(dst_text.work_value);

			text = std::string("JMP ") + dst_text.operand;
		}

		if ((instruction & 0b1111111000000000) == 0b0000100000000000) {
			auto addressing = addressing_to_string(dst_register, (addr + 2) & 65535, word_mode);
			if (addressing.has_value() == false)
				return { };
			auto dst_text { addressing.value() };

			auto next_word = dst_text.instruction_part;
			if (next_word != -1)
				instruction_words.push_back(next_word);

			work_values.push_back(dst_text.work_value);

			text = format("JSR R%d,", src_register & 7) + dst_text.operand;
		}

		if ((instruction & 0b1111111111111000) == 0b0000000010000000)
			text = "RTS";
	}

	if (text.empty())
		text = "???";

	std::map<std::string, std::vector<std::string> > out;

	// MOV x,y
	out.insert({ "address", { format("%06o", addr) } });

	// MOV x,y
	out.insert({ "instruction-text", { text } });

	// words making up the instruction
	std::vector<std::string> instruction_values;
	for(auto i : instruction_words)
		instruction_values.push_back(format("%06o", i));

	out.insert({ "instruction-values", instruction_values });

	// R0-R5, SP, PC
	std::vector<std::string> registers;

	for(int i=0; i<8; i++) {
		if (i < 6)
			registers.push_back(format("%06o", get_register(i)));
		else if (i == 6)
			registers.push_back(format("%06o", sp[psw >> 14]));
		else
			registers.push_back(format("%06o", addr));
	}

	out.insert({ "registers", registers });

	std::vector<std::string> registers_sp;

	for(int i=0; i<4; i++)
		registers_sp.push_back(format("%06o", sp[i]));

	out.insert({ "sp", registers_sp });

	// PSW
	std::string psw_str = format("%d%d|%d|%d|%c%c%c%c%c", psw >> 14, (psw >> 12) & 3, (psw >> 11) & 1, (psw >> 5) & 7,
                        psw & 16?'t':'-', psw & 8?'n':'-', psw & 4?'z':'-', psw & 2 ? 'v':'-', psw & 1 ? 'c':'-');
	out.insert({ "psw", { std::move(psw_str) } });
	out.insert({ "psw-value", { format("%06o", psw) } });

	// values worked with
	std::vector<std::string> work_values_str;
	for(auto v : work_values)
		work_values_str.push_back(format("%06o", v));
	out.insert({ "work-values", work_values_str });

	out.insert({ "MMR0", { format("%06o", b->getMMU()->getMMR0()) } });
	out.insert({ "MMR1", { format("%06o", b->getMMU()->getMMR1()) } });
	out.insert({ "MMR2", { format("%06o", b->getMMU()->getMMR2()) } });
	out.insert({ "MMR3", { format("%06o", b->getMMU()->getMMR3()) } });

	return out;
}

void cpu::step()
{
	it_is_a_trap = false;

	if (!b->getMMU()->isMMR1Locked())
		b->getMMU()->clearMMR1();

	if (any_queued_interrupts && execute_any_pending_interrupt()) {
		if (!b->getMMU()->isMMR1Locked())
			b->getMMU()->clearMMR1();
	}

	instruction_count++;

	try {
		instruction_start = getPC();

		if (!b->getMMU()->isMMR1Locked())
			b->getMMU()->setMMR2(instruction_start);

		uint16_t instr = b->read_word(instruction_start);

		add_register(7, 2);

		if (double_operand_instructions(instr))
			return;

		if (conditional_branch_instructions(instr))
			return;

		if (condition_code_operations(instr))
			return;

		if (misc_operations(instr))
			return;

		DOLOG(warning, false, "UNHANDLED instruction %06o @ %06o", instr, instruction_start);

		trap(010);  // floating point nog niet geimplementeerd
	}
	catch(const int exception_nr) {
		TRACE("bus-trap during execution of command (%d)", exception_nr);
	}
}

JsonDocument cpu::serialize()
{
	JsonDocument j;

	for(int set=0; set<2; set++) {
		for(int regnr=0; regnr<6; regnr++)
			j[format("register-%d-%d", set, regnr)] = regs0_5[set][regnr];
	}

	for(int spnr=0; spnr<4; spnr++)
		j[format("sp-%d", spnr)] = sp[spnr];

        j["pc"]                    = pc;
        j["instruction_start"]     = instruction_start;
        j["psw"]                   = psw;
        j["fpsr"]                  = fpsr;
        j["stackLimitRegister"]    = stackLimitRegister;
        j["processing_trap_depth"] = processing_trap_depth;
        j["instruction_count"]     = instruction_count;
        j["running_since"]         = running_since;
        j["wait_time"]             = wait_time;
        j["it_is_a_trap"]          = it_is_a_trap;
        j["debug_mode"]            = debug_mode;

	if (trap_delay.has_value())
		j["trap_delay"] = trap_delay.value();

	JsonVariant j_queued_interrupts;
	for(auto & il: queued_interrupts) {
		JsonDocument ja_qi_level;
		JsonArray ja_qi_level_work = ja_qi_level.to<JsonArray>();
		for(auto v: il.second)
			ja_qi_level_work.add(v);

		j_queued_interrupts[format("%d", il.first)] = ja_qi_level;
	}

	j["queued_interrupts"]     = j_queued_interrupts;

	j["any_queued_interrupts"] = bool(any_queued_interrupts);

	return j;
}

cpu *cpu::deserialize(const JsonVariantConst j, bus *const b, std::atomic_uint32_t *const event)
{
	cpu *c = new cpu(b, event);

	for(int set=0; set<2; set++) {
		for(int regnr=0; regnr<6; regnr++)
			c->regs0_5[set][regnr] = j[format("register-%d-%d", set, regnr)];
	}

	for(int spnr=0; spnr<4; spnr++)
		c->sp[spnr] = j[format("sp-%d", spnr)];

        c->pc                    = j["pc"];
        c->instruction_start     = j["instruction_start"];
        c->psw                   = j["psw"];
        c->fpsr                  = j["fpsr"];
        c->stackLimitRegister    = j["stackLimitRegister"];
        c->processing_trap_depth = j["processing_trap_depth"];
        c->instruction_count     = j["instruction_count"];
        c->running_since         = get_us();
        c->wait_time             = 0;
        c->it_is_a_trap          = j["it_is_a_trap"];
        c->debug_mode            = j["debug_mode"];

	if (j.containsKey("trap_delay"))
		c->trap_delay    = j["trap_delay"];
	else
		c->trap_delay.reset();

	c->any_queued_interrupts = j["any_queued_interrupts"].as<bool>();

	c->init_interrupt_queue();
	for(int level=0; level<8; level++) {
		auto it = c->queued_interrupts.find(level);

		JsonArrayConst ja_qi_level = j["queued_interrupts"][format("%d", level)].as<JsonArrayConst>();
		for(auto v : ja_qi_level)
			it->second.insert(v.as<int>());
	}

	return c;
}
