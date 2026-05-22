// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "breakpoint.h"
#include "bus.h"
#include "cpu.h"
#include "log.h"
#include "utils.h"


#define SIGN(x, wm) ((wm) == wm_byte ? (x) & 0x80 : (x) & 0x8000)

#define IS_0(x, wm) ((wm) == wm_byte ? ((x) & 0xff) == 0 : (x) == 0)

constexpr const uint16_t word_mode_mask[2] { 0xffff, 0xff };

cpu::cpu(bus *const b, kek_event_t *const event) : b(b), mmu_(b->getMMU()), event(event)
{
	reset();

#if defined(FREERTOS)
	xSemaphoreGive(qi_lock);  // initialize
#endif
}

cpu::~cpu()
{
}

void cpu::init_interrupt_queue()
{
	for(uint8_t level=0; level<8; level++)
		queued_interrupts[level].clear();
}

std::optional<std::pair<breakpoint &, const std::string> > cpu::check_breakpoint()
{
	for(auto & bp: breakpoints) {
		auto rc = bp.second->is_triggered();
		if (rc.has_value())
			return { { *bp.second, rc.value() } };
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

std::unordered_map<int, breakpoint *> cpu::list_breakpoints()
{
	return breakpoints;
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
	memset(sp,      0x00, sizeof sp     );
	pc   = 0;
	psw  = 0;  // 7 << 5;
	fpsr = 0;
	init_interrupt_queue();

        it_is_a_trap          = false;
	processing_trap_depth = 0;
	kw11l_counter         = 0;
}

uint16_t cpu::get_register(const int nr) const
{
	assert(nr >= 0 && nr < 8);
	if (nr < 6) {
		int set = get_register_set();
		return regs0_5[set][nr];
	}

	if (nr == 6)
		return sp[getPSW_runmode()];

	return pc;
}

uint16_t *cpu::get_register_pointer(const int nr)
{
	assert(nr >= 0 && nr < 8);
	if (nr < 6) {
		int set = get_register_set();
		return &regs0_5[set][nr];
	}

	if (nr == 6)
		return &sp[getPSW_runmode()];

	return &pc;
}

void cpu::set_register(const int nr, const uint16_t value)
{
	assert(nr >= 0 && nr < 8);
	if (nr < 6) {
		int set = get_register_set();

		regs0_5[set][nr] = value;
	}
	else if (nr == 6)
		sp[getPSW_runmode()] = value;
	else {
		pc = value;
	}
}

void cpu::set_registerLowByte(const int nr, const word_mode_t word_mode, const uint16_t value)
{
	if (word_mode == wm_byte) {
		assert(value < 256);
		uint16_t *const vp = get_register_pointer(nr);
		(*vp) &= 0xff00;
		(*vp) |= value;
	}
	else {
		set_register(nr, value);
	}
}

bool cpu::put_result(const gam_rc_t & g, const uint16_t value)
{
	if (g.is_addr == false) {
		set_registerLowByte(g.reg, g.word_mode, value);

		return true;
	}

	return b->write(g.addr, g.word_mode, value, getPSW_runmode(), g.space) == false;
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

uint16_t cpu::lowlevel_register_get(const uint8_t set, const uint8_t reg) const
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
		int cur_mode  = std::max( v >> 14,       psw >> 14);
		int prev_mode = std::max((v >> 12) & 3, (psw >> 12) & 3);
		psw = (psw & 004340) | (v & 037) | (cur_mode << 14) | (prev_mode << 12);
	}
	else {
		psw = v & 0174377;  // mask off reserved bits
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
	uint8_t start_level = getPSW_spl() + 1;

	for(uint8_t i=start_level; i < 8; i++) {
		if (queued_interrupts[i].empty() == false)
			return true;
	}

	return false;
}

void cpu::execute_any_pending_interrupt()
{
#if defined(FREERTOS)
	xSemaphoreTake(qi_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(qi_lock);
#endif
	any_queued_interrupts = false;

	uint8_t current_level = getPSW_spl();

	// uint8_t start_level = current_level <= 3 ? 0 : current_level + 1;
	// PDP-11_70_Handbook_1977-78.pdf page 1-5, "processor priority"
	uint8_t start_level   = current_level + 1;

	for(uint8_t i=0; i < 8; i++) {
		if (queued_interrupts[i].empty() == false) {
			any_queued_interrupts = true;

			if (i < start_level)  // at least we know now that there's an interrupt scheduled
				continue;

			auto     vector = queued_interrupts[i].begin();
			uint16_t v      = *vector;
			queued_interrupts[i].erase(vector);

			if (cnsl) {
				if (v == 0100) {  // 50 Hz interrupt
					if (++kw11l_counter >= 25) {
						kw11l_counter = 0;
						cnsl->toggle_LED_state();
					}
				}
				else {
					cnsl->toggle_LED_state();
				}
			}

			TRACE("Invoking interrupt vector %o (IPL %d, current: %d)", v, i, current_level);
			trap(v, i, true);

#if defined(FREERTOS)
			xSemaphoreGive(qi_lock);
#endif
			return;
		}
	}

#if defined(FREERTOS)
	xSemaphoreGive(qi_lock);
#endif
}

void cpu::queue_interrupt(const uint8_t level, const uint16_t vector)
{
#if defined(FREERTOS)
	xSemaphoreTake(qi_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(qi_lock);
#endif

	queued_interrupts[level].insert(vector);
	TRACE("Queueing interrupt vector %o (IPL %d, current: %d), n: %" PRIzu "", vector, level, getPSW_spl(), queued_interrupts[level].size());

#if defined(FREERTOS)
	xSemaphoreGive(qi_lock);

	if (uxQueueMessagesWaiting(qi_q) == 0) {
		uint8_t value = 1;
		xQueueSend(qi_q, &value, portMAX_DELAY);
	}
#else
	qi_cv.notify_one();
#endif

	any_queued_interrupts = true;
}

void cpu::unqueue_interrupt(const uint8_t level, const uint16_t vector)
{
#if defined(FREERTOS)
	xSemaphoreTake(qi_lock, portMAX_DELAY);
#else
	std::unique_lock<std::mutex> lck(qi_lock);
#endif

	queued_interrupts[level].erase(vector);

#if defined(FREERTOS)
	xSemaphoreGive(qi_lock);
#endif
}

void cpu::add_to_MMR1(const int reg, const int delta)
{
	assert(reg >= 0 && reg < 8);
	assert(delta >= -2 && delta <= 2);

	if (mmu_->isMMR1Locked() == false) {
		TRACE("MMR1: add %d to register R%d", delta, reg);
		mmu_->add_to_MMR1(delta, reg);
	}
}

// GAM = general addressing modes
gam_rc_t cpu::getGAM(const uint8_t mode, const uint8_t reg, const word_mode_t word_mode, const bool read_value)
{
	d_i_space_t isR7_space = reg == 7 ? i_space : (mmu_->get_use_data_space(getPSW_runmode()) ? d_space : i_space);
	//                                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ always d_space here? TODO
	gam_rc_t    g { word_mode, isR7_space, !!mode, 0, { } };

        uint16_t temp      = 0;
	uint16_t next_word = 0;
	int      delta     = 0;

	switch(mode) {
		case 0:  // Rn
			g.reg   = reg;
			g.value = get_register(reg) & word_mode_mask[word_mode];
			break;
		case 1:  // (Rn)
			g.addr  = get_register(reg);
			if (read_value)
				g.value = b->read(g.addr, word_mode, getPSW_runmode(), isR7_space);
			break;
		case 2:  // (Rn)+  /  #n
			g.addr  = get_register(reg);
			delta   = word_mode == wm_word || reg >= 6 ? 2 : 1;
			add_register(reg, delta);
			add_to_MMR1(reg, delta);
			if (read_value)
				g.value = b->read(g.addr, word_mode, getPSW_runmode(), isR7_space);
			break;
		case 3:  // @(Rn)+  /  @#a
			g.addr  = b->read(get_register(reg), wm_word, getPSW_runmode(), isR7_space);
			add_to_MMR1(reg, 2);
			g.space = d_space;
			// might be wrong: the adds should happen when the read is really performed(?), because of traps
			add_register(reg, 2);
			if (read_value)
				g.value = b->read(g.addr, word_mode, getPSW_runmode(), g.space);
			break;
		case 4:  // -(Rn)
			delta   = word_mode == wm_word || reg >= 6 ? -2 : -1;
			temp    = add_register(reg, delta);
			add_to_MMR1(reg, delta);
			g.space = d_space;
			g.addr  = temp;
			if (read_value)
				g.value = b->read(g.addr, word_mode, getPSW_runmode(), isR7_space);
			break;
		case 5:  // @-(Rn)
			temp    = add_register(reg, -2);
			add_to_MMR1(reg, -2);
			g.addr  = b->read(temp, wm_word, getPSW_runmode(), isR7_space);
			g.space = d_space;
			if (read_value)
				g.value = b->read(g.addr, word_mode, getPSW_runmode(), g.space);
			break;
		case 6:  // x(Rn)  /  a
			next_word = b->read(getPC(), wm_word, getPSW_runmode(), i_space);
			add_register(7, + 2);
			g.addr  = get_register(reg) + next_word;
			g.space = d_space;
			if (read_value)
				g.value = b->read(g.addr, word_mode, getPSW_runmode(), g.space);
			break;
		case 7:  // @x(Rn)  /  @a
			next_word = b->read(getPC(), wm_word, getPSW_runmode(), i_space);
			add_register(7, + 2);
			g.addr  = b->read(get_register(reg) + next_word, wm_word, getPSW_runmode(), d_space);
			g.space = d_space;
			if (read_value)
				g.value = b->read(g.addr, word_mode, getPSW_runmode(), g.space);
			break;
	}

	assert(g.value < 256 || word_mode == wm_word);

	return g;
}

bool cpu::putGAM(const gam_rc_t & g, const uint16_t value)
{
	assert(value < 256 || g.word_mode == wm_word);

	if (g.is_addr) {
		auto rc = b->write(g.addr, g.word_mode, value, getPSW_runmode(), g.space);
		return rc == false;
	}

	set_register(g.reg, value);

	return true;
}

gam_rc_t cpu::getGAMAddress(const uint8_t mode, const int reg, const word_mode_t word_mode)
{
	return getGAM(mode, reg, word_mode, false);
}

bool cpu::double_operand_instructions(const uint16_t instr)
{
	const uint8_t     operation = (instr >> 12) & 7;
	const word_mode_t word_mode = instr & 0x8000 ? wm_byte : wm_word;

	const uint8_t src        = (instr >> 6) & 63;
	const uint8_t src_mode   = (src >> 3) & 7;
	const uint8_t src_reg    = src & 7;

	const uint8_t dst        = instr & 63;
	const uint8_t dst_mode   = (dst >> 3) & 7;
	const uint8_t dst_reg    = dst & 7;

	switch(operation) {
		case 0b000:
			return single_operand_instructions(instr);

		case 0b001: { // MOV/MOVB Move Word/Byte
				    gam_rc_t g_src     = getGAM(src_mode, src_reg, word_mode);
				    bool     set_flags = true;

				    if (word_mode == wm_byte && dst_mode == 0) {
					    set_register(dst_reg, int8_t(g_src.value.value()));  // int8_t: sign extension
				    }
				    else {
					    auto g_dst = getGAMAddress(dst_mode, dst_reg, word_mode);
					    set_flags = putGAM(g_dst, g_src.value.value());
				    }

				    if (set_flags)
					    setPSW_flags_nzv(g_src.value.value(), word_mode);

				    return true;
			    }

		case 0b010: { // CMP/CMPB Compare Word/Byte
				    gam_rc_t g_src = getGAM(src_mode, src_reg, word_mode);
				    auto     g_dst = getGAM(dst_mode, dst_reg, word_mode);

				    uint16_t temp  = (g_src.value.value() - g_dst.value.value()) & word_mode_mask[word_mode];

				    setPSW_n(SIGN(temp, word_mode));
				    setPSW_z(IS_0(temp, word_mode));
				    setPSW_v(SIGN((g_src.value.value() ^ g_dst.value.value()) & (~g_dst.value.value() ^ temp), word_mode));
				    setPSW_c(g_src.value.value() < g_dst.value.value());

				    return true;
			    }

		case 0b011: { // BIT/BITB Bit Test Word/Byte
				    gam_rc_t g_src  = getGAM(src_mode, src_reg, word_mode);
				    auto     g_dst  = getGAM(dst_mode, dst_reg, word_mode);

				    uint16_t result = (g_dst.value.value() & g_src.value.value()) & word_mode_mask[word_mode];

				    setPSW_flags_nzv(result, word_mode);

				    return true;
			    }

		case 0b100: { // BIC/BICB Bit Clear Word/Byte
				  gam_rc_t g_src  = getGAM(src_mode, src_reg, word_mode);

				  if (dst_mode == 0) {
					  uint16_t v      = get_register(dst_reg);  // need the full word
					  uint16_t result = v & ~g_src.value.value();

					  set_register(dst_reg, result);

					  setPSW_flags_nzv(result, word_mode);
				  }
				  else {
					  auto     g_dst  = getGAM(dst_mode, dst_reg, word_mode);
					  uint16_t result = g_dst.value.value() & ~g_src.value.value();

					  if (put_result(g_dst, result))
						  setPSW_flags_nzv(result, word_mode);
				  }

				  return true;
			    }

		case 0b101: { // BIS/BISB Bit Set Word/Byte
				  gam_rc_t g_src  = getGAM(src_mode, src_reg, word_mode);

				  if (dst_mode == 0) {
					  uint16_t v      = get_register(dst_reg);  // need the full word
					  uint16_t result = v | g_src.value.value();

					  set_register(dst_reg, result);

					  setPSW_n(SIGN(result, word_mode));
					  setPSW_z(IS_0(result, word_mode));
					  setPSW_v(false);
				  }
				  else {
					  auto     g_dst  = getGAM(dst_mode, dst_reg, word_mode);
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
				    int16_t  result = 0;

				    if (instr & 0x8000)  // SUB
					    result = g_dst.value.value() - g_ssrc.value.value();
				    else  // ADD
					    result = g_dst.value.value() + g_ssrc.value.value();

				    bool set_flags = putGAM(g_dst, result);

				    if (set_flags) {
					    if (instr & 0x8000) {  // SUB
						    setPSW_v(SIGN((g_dst.value.value() ^ g_ssrc.value.value()) & (~g_ssrc.value.value() ^ result), wm_word));
						    setPSW_c(uint16_t(g_dst.value.value()) < uint16_t(g_ssrc.value.value()));
					    }
					    else {
						    setPSW_v(SIGN((~g_ssrc.value.value() ^ g_dst.value.value()) & (g_ssrc.value.value() ^ (result & 0xffff)), wm_word));
						    setPSW_c(uint16_t(result) < uint16_t(g_ssrc.value.value()));
					    }

					    setPSW_n(result < 0);
					    setPSW_z(result == 0);
				    }

				    return true;
			    }

		case 0b111: {
			if (word_mode == wm_byte) [[unlikely]]
				return false;

			return additional_double_operand_instructions(instr);
		}
	}

	return false;
}

uint32_t cpu::shifter(uint32_t value, int shift, bool is32b)
{
	uint64_t sign_extend = is32b ? B64_MSWSET : (B64_MSWSET | B32_MSWSET);
	uint32_t sign_mask   = is32b ? B32_MSBSET : B16_MSBSET;
	uint32_t mask        = is32b ? 0xffffffff : 0xffff;
	bool     sign        = value & sign_mask;

	TRACE("shift %012o with %d", value, shift);

	setPSW_v(false);

	if (shift == 0)
		setPSW_c(false);
	else if (shift < 32) {
		setPSW_c((value << (shift - 1)) & sign_mask);

		for(int i=0; i<shift; i++) {
			value <<= 1;
			if (bool(value & sign_mask) != sign)
				setPSW_v(true);
		}
	}
	else if (shift == 32) {
		value = -sign;
		setPSW_c(sign);
		setPSW_v(sign != bool(value & sign_mask));
	}
	else {
		int shift_n = (64 - shift) - 1;

		// extend sign-bit
		if (sign) {  // convert to unsigned 64b int & extend sign
			value = (uint64_t(value) | sign_extend) >> shift_n;
			setPSW_c(value & 1);
			value = (uint64_t(value) | sign_extend) >> 1;
		}
		else {
			value >>= shift_n;
			setPSW_c(value & 1);
			value >>= 1;
		}

		bool new_sign = value & sign_mask;
		setPSW_v(sign != new_sign);
	}

	value &= mask;
	setPSW_n(value & sign_mask);
	setPSW_z(value == 0);

	return value;
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
				int16_t R2  = R2g.value.value();

				int32_t result = R1 * R2;

				set_register(reg, result >> 16);
				set_register(reg | 1, result);

				setPSW_n(result < 0);
				setPSW_z(result == 0);
				setPSW_v(false);
				setPSW_c(result < -32768 || result > 32767);
				return true;
			}

		case 1: { // DIV
				auto    R2g     = getGAM(dst_mode, dst_reg, wm_word);
				int16_t divider = R2g.value.value();
				int32_t R0R1    = (uint32_t(get_register(reg)) << 16) | get_register(reg | 1);

				if (divider == 0) {  // divide by zero
					setPSW_n(false);
					setPSW_z(true);
					setPSW_v(true);
					setPSW_c(true);

					return true;
				}
				else if (divider == -1 && uint32_t(R0R1) == B32_MSBSET) {  // maximum negative value; too big
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
				uint32_t R         = get_register(reg);
			        auto     g_dst     = getGAM(dst_mode, dst_reg, wm_word);
				int      shift     = g_dst.value.value() & 077;
				uint32_t new_value = shifter(R, shift, false);

				set_register(reg, new_value);

				return true;
			}

		case 3: { // ASHC
				uint32_t R0R1      = (uint32_t(get_register(reg)) << 16) | get_register(reg | 1);
			        auto     g_dst     = getGAM(dst_mode, dst_reg, wm_word);
				int      shift     = g_dst.value.value() & 077;
				uint32_t new_value = shifter(R0R1, shift, true);

				set_register(reg,     new_value >> 16  );
				set_register(reg | 1, new_value);

				return true;
			}

		case 4: { // XOR (word only)
			  	uint16_t reg_v = get_register(reg);  // in case it is R7
			        auto     g_dst = getGAM(dst_mode, dst_reg, wm_word);
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

						  v = (v + 1) & word_mode_mask[word_mode];
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(word_mode == wm_byte ? (v & 0xff) == 0x80 : v == 0x8000);

						  set_register(dst_reg, v);
					  }
					  else {
						  auto    a         = getGAM(dst_mode, dst_reg, word_mode);
						  int32_t vl        = (a.value.value() + 1) & word_mode_mask[word_mode];

						  bool    set_flags = b->write(a.addr, a.word_mode, vl, getPSW_runmode(), a.space) == false;

						  if (set_flags) {
							  setPSW_n(SIGN(vl, word_mode));
							  setPSW_z(IS_0(vl, word_mode));
							  setPSW_v(word_mode == wm_byte ? vl == 0x80 : vl == 0x8000);
						  }
					  }

					  break;
				  }

		case 0b000101011: { // DEC/DECB
					  if (dst_mode == 0) {
						  uint16_t v   = get_register(dst_reg);
						  uint16_t add = word_mode == wm_byte ? v & 0xff00 : 0;

						  v = (v - 1) & word_mode_mask[word_mode];
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(word_mode == wm_byte ? (v & 0xff) == 0x7f : v == 0x7fff);

						  set_register(dst_reg, v);
					  }
					  else {
						  auto     a         = getGAM(dst_mode, dst_reg, word_mode);
						  int32_t  vl        = (a.value.value() - 1) & word_mode_mask[word_mode];

						  bool     set_flags = b->write(a.addr, a.word_mode, vl, getPSW_runmode(), a.space) == false;

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

						  v = (-v) & word_mode_mask[word_mode];
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v(word_mode == wm_byte ? (v & 0xff) == 0x80 : v == 0x8000);
						  setPSW_c(v & word_mode_mask[word_mode]);

						  set_register(dst_reg, v);
					  }
					  else {
						  auto     a = getGAM(dst_mode, dst_reg, word_mode);
						  uint16_t v = -a.value.value();

						  bool set_flags = b->write(a.addr, a.word_mode, v, getPSW_runmode(), a.space) == false;

						  if (set_flags) {
							  setPSW_n(SIGN(v, word_mode));
							  setPSW_z(IS_0(v, word_mode));
							  setPSW_v(word_mode == wm_byte ? (v & 0xff) == 0x80 : v == 0x8000);
							  setPSW_c(v & word_mode_mask[word_mode]);
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

						  v = (v + org_c) & word_mode_mask[word_mode];
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v((word_mode == wm_byte ? (vo & 0xff) == 0x7f : vo == 0x7fff) && org_c);
						  setPSW_c((word_mode == wm_byte ? (vo & 0xff) == 0xff : vo == 0xffff) && org_c);

						  set_register(dst_reg, v);
					  }
					  else {
						  auto           a     = getGAM(dst_mode, dst_reg, word_mode);
						  const uint16_t vo    = a.value.value();
						  bool           org_c = getPSW_c();
						  uint16_t       v     = (vo + org_c) & (word_mode == wm_byte ? 0x00ff : 0xffff);

						  bool set_flags = b->write(a.addr, a.word_mode, v, getPSW_runmode(), a.space) == false;

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

						  v = (v - org_c) & word_mode_mask[word_mode];
						  v |= add;

						  setPSW_n(SIGN(v, word_mode));
						  setPSW_z(IS_0(v, word_mode));
						  setPSW_v((word_mode == wm_byte ? (vo & 0xff) == 0x80 : vo == 0x8000) && org_c);
						  setPSW_c(IS_0(vo, word_mode) && org_c);

						  set_register(dst_reg, v);
					  }
					  else {
						  auto           a     = getGAM(dst_mode, dst_reg, word_mode);
						  const uint16_t vo    = a.value.value();
						  bool           org_c = getPSW_c();
						  uint16_t       v     = (vo - org_c) & word_mode_mask[word_mode];

						  bool set_flags = b->write(a.addr, a.word_mode, v, getPSW_runmode(), a.space) == false;

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
						  uint16_t t         = a.value.value();
						  bool     new_carry = t & 1;

						  uint16_t temp = 0;
						  if (word_mode == wm_byte)
							  temp = (t >> 1) | (getPSW_c() <<  7);
						  else
							  temp = (t >> 1) | (getPSW_c() << 15);

						  bool set_flags = b->write(a.addr, a.word_mode, temp, getPSW_runmode(), a.space) == false;

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

						  bool set_flags = b->write(a.addr, a.word_mode, temp, getPSW_runmode(), a.space) == false;

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
						  uint16_t v   = a.value.value();

						  uint16_t hb  = word_mode == wm_byte ? v & 128 : v & 32768;

						  setPSW_c(v & 1);

						  if (word_mode == wm_byte)
							  v = ((v & 255) >> 1) | (v & 0xff00);
						  else
							  v >>= 1;
						  v |= hb;

						  bool set_flags = b->write(a.addr, a.word_mode, v, getPSW_runmode(), a.space) == false;

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
						 uint16_t v   = ((vl << 1) & word_mode_mask[word_mode]);

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
						 uint16_t vl  = a.value.value();
						 uint16_t v   = (vl << 1) & word_mode_mask[word_mode];

						 bool set_flags = b->write(a.addr, a.word_mode, v, getPSW_runmode(), a.space) == false;

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

						// read from previous space
						v = b->read(a.addr, wm_word, getPSW_prev_runmode(), word_mode == wm_byte ? d_space : i_space);
					 }

					 setPSW_flags_nzv(v, wm_word);

					 // put on current stack
					 push_stack(v);
					 break;
				 }

		case 0b00110110: { // MTPI/MTPD
					 // always words: word_mode-bit is to select between MTPI and MTPD

					 // retrieve word from '15/14'-stack
					 uint16_t v             = pop_stack();
					 bool     set_flags     = true;

					 if (dst_mode == 0) {
						if (dst_reg == 6)
							sp[getPSW_prev_runmode()] = v;
						else
							set_register(dst_reg, v);
					 }
					 else {
						auto a = getGAMAddress(dst_mode, dst_reg, wm_word);
						// mmu_->mmudebug(a.addr);
						set_flags = b->write(a.addr, wm_word, v, getPSW_prev_runmode(), word_mode == wm_byte ? d_space : i_space) == false;
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

					 set_register(5, pop_stack());
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

std::optional<bool> cpu::conditional_branch_instructions_evaluate(const uint16_t instr) const
{
	switch(instr >> 8) {
		case 0b00000001: // BR
			return true;

		case 0b00000010: // BNE
			return !getPSW_z();

		case 0b00000011: // BEQ
			return getPSW_z();

		case 0b00000100: // BGE
			return getPSW_n() == getPSW_v();

		case 0b00000101: // BLT
			return getPSW_n() ^ getPSW_v();

		case 0b00000110: // BGT
			return getPSW_n() == getPSW_v() && getPSW_z() == false;

		case 0b00000111: // BLE
			return getPSW_n() != getPSW_v() || getPSW_z();

		case 0b10000000: // BPL
			return getPSW_n() == false;

		case 0b10000001: // BMI
			return getPSW_n() == true;

		case 0b10000010: // BHI
			return getPSW_c() == false && getPSW_z() == false;

		case 0b10000011: // BLOS
			return getPSW_c() || getPSW_z();

		case 0b10000100: // BVC
			return getPSW_v() == false;

		case 0b10000101: // BVS
			return getPSW_v();

		case 0b10000110: // BCC
			return getPSW_c() == false;

		case 0b10000111: // BCS / BLO
			return getPSW_c();
	}

	return { };
}

bool cpu::conditional_branch_instructions(const uint16_t instr)
{
	const int8_t  offset = instr;
	auto          take   = conditional_branch_instructions_evaluate(instr);
	if  (take.has_value() == false)
		return false;

	if (take.value())
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

void cpu::push_stack(const uint16_t v)
{
	if (getPSW_runmode() == 0) {
		uint16_t use_limit = stack_limit_register == 0 ? 0400 : stack_limit_register;
		uint16_t sp        = get_register(6);

		if (sp < use_limit) {
			if (sp >= use_limit - 32) {  // yellow zone
				uint16_t a = add_register(6, -2);
				b->write_word(a, v, d_space);
				delayed_trap = 04;
				mmu_->setCPUERRBit(010);
			}
			else {
				set_register(6, 4);  // red zone
				trap(04, 7);
				delayed_trap = 04;
				processing_trap_depth = 127;  // double trap so halt
			}
			return;
		}
	}

	uint16_t a = add_register(6, -2);
	b->write_word(a, v, d_space);
}

uint16_t cpu::pop_stack()
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
				bool     int_  = false;

				do
				{
					// wait intervals of 100 ms. if no interrupt for 1,5 seconds, then maybe things are stuck.
#if defined(FREERTOS)
					uint8_t rc = 0;
					xQueueReceive(qi_q, &rc, check_pending_interrupts() ? 0 : 100 / portTICK_PERIOD_MS);
#else
					using namespace std::chrono_literals;
					std::unique_lock<std::mutex> lck(qi_lock);
					if (check_pending_interrupts() == false)
						qi_cv.wait_for(lck, 100 * 1ms);
					lck.unlock();
#endif
					if (wait_stuck == false && get_us() - start > 1500000) {
						wait_stuck = true;
						DOLOG(debug, true, "cpu: WAIT/KW11-L stuck? no interrupts seen for 1,5 seconds");
					}
				}
				while(check_pending_interrupts() == false);
			}

			TRACE("WAIT returned");

			return true;

		case 0b0000000000000010: // RTI
			if (debug_mode)
				pop_from_stack_trace();
			setPC(pop_stack());
			setPSW(pop_stack(), !!getPSW_runmode());
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
			setPC(pop_stack());
			setPSW(pop_stack(), !!getPSW_runmode());
			return true;

		case 0b0000000000000111: // MFPT
			//set_register(0, 0);
			trap(010); // does not exist on PDP-11/70
			return true;

		case 0b0000000000000101: // RESET
			if (getPSW_runmode() == 0) {  // only in kernel mode
				b->reset(true);
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
		setPC(g.addr);

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
		auto dst_value = a.addr;

		int  link_reg  = (instr >> 6) & 7;

		// PUSH link
		push_stack(get_register(link_reg));
		if (!mmu_->isMMR1Locked())
			mmu_->add_to_MMR1(-2, 6);

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

	auto it = trap_counts.find(vector);
	if (it == trap_counts.end())
		trap_counts.insert({ vector, 1 });
	else
		it->second++;
	trap_counter++;

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
			before_psw = getPSW();
			before_pc  = getPC();

			if (debug_mode)
				add_to_stack_trace(instruction_start);

			// make sure the trap vector is retrieved from kernel space
			psw &= 037777;  // mask off 14/15 to make it into kernel-space

			auto space = mmu_->get_use_data_space(0) ? d_space : i_space;
			setPC(b->read_word(vector + 0, space));

			// switch to kernel mode & update 'previous mode'
			uint16_t new_psw = b->read_word(vector + 2, space) & 0147777;  // mask off old 'previous mode'
			if (new_ipl != -1)
				new_psw = (new_psw & ~0xe0) | (new_ipl << 5);
			new_psw |= (before_psw >> 2) & 030000; // apply new 'previous mode'
			setPSW(new_psw, false);

			if (processing_trap_depth >= 2 && kernel_mode)
				set_register(6, 04);

			uint16_t prev_sp = get_register(6);
			try {
				push_stack(before_psw);
				push_stack(before_pc);
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

cpu::operand_parameters cpu::addressing_to_string(const uint8_t mode_register, const uint16_t pc, const word_mode_t word_mode) const
{
	assert(mode_register < 64);

	int         run_mode  = getPSW_runmode();
	auto        temp      = b->peek_word(run_mode, pc);
	if (temp.has_value() == false) {
		operand_parameters out { };
		out.error = "cannot read from memory";
		return out;
	}
	uint16_t    next_word = temp.value();
	int         reg       = mode_register & 7;
	uint16_t    mask      = word_mode_mask[word_mode];

	std::optional<uint16_t> temp2;
	bool        valid     = true;

	std::string reg_name;
	if (reg == 6)
		reg_name = "SP";
	else if (reg == 7)
		reg_name = "PC";
	else
		reg_name = format("R%d", reg);

	std::optional<std::string> error;
	int mode = mode_register >> 3;

	switch(mode) {
		case 0:
			return { reg_name, 2, -1, uint16_t(get_register(reg) & mask), true, { } };

		case 1:
			temp2 = b->peek_word(run_mode, get_register(reg));
			if (temp2.has_value() == false)
				temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", get_register(reg));

			return { "(" + reg_name + ")", 2, -1, uint16_t(temp2.value() & mask), valid, error };

		case 2:
			if (reg == 7)
				return { format("#%06o", next_word), 4, int(next_word), uint16_t(next_word & mask), true, { } };

			temp2 = b->peek_word(run_mode, get_register(reg));
			if (temp2.has_value() == false)
				temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", get_register(reg));

			return { "(" + reg_name + ")+", 2, -1, uint16_t(temp2.value() & mask), valid, error };

		case 3:
			if (reg == 7) {
				temp2 = b->peek_word(run_mode, next_word);
				if (temp2.has_value() == false)
					temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", next_word);

				return { format("@#%06o", next_word), 4, int(next_word), uint16_t(temp2.value() & mask), valid, error };
			}

			temp2 = b->peek_word(run_mode, get_register(reg));
			if (temp2.has_value() == false)
				temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", get_register(reg));
			else {
				uint16_t keep = temp2.value();
				temp2 = b->peek_word(run_mode, temp2.value());
				if (temp2.has_value() == false)
					temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", keep);
			}

			return { "@(" + reg_name + ")+", 2, -1, uint16_t(temp2.value() & mask), valid, error };

		case 4: {
				uint16_t calculated_address = get_register(reg) - (word_mode == wm_word || reg >= 6 ? 2 : 1);
				temp2 = b->peek_word(run_mode, calculated_address);
				if (temp2.has_value() == false)
					temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", calculated_address);

				return { "-(" + reg_name + ")", 2, -1, uint16_t(temp2.value() & mask), valid, error };
			}

		case 5: {
				uint16_t calculated_address = get_register(reg) - 2;
				temp2 = b->peek_word(run_mode, calculated_address);
				if (temp2.has_value() == false)
					temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", calculated_address);
				else {
					temp2 = b->peek_word(run_mode, temp2.value());
					if (temp2.has_value() == false)
						temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", temp2.value());
				}

				return { "@-(" + reg_name + ")", 2, -1, uint16_t(temp2.value() & mask), valid, error };
			}

		case 6:
			{
				uint16_t calculated_address = get_register(reg) + next_word;
				temp2 = b->peek_word(run_mode, calculated_address);
				if (temp2.has_value() == false)
					temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", calculated_address);

				if (reg == 7)
					return { format("%06o", (pc + next_word + 2) & 65535), 4, int(next_word), uint16_t(temp2.value() & mask), valid, error };

				return { format("%o(%s)", next_word, reg_name.c_str()), 4, int(next_word), uint16_t(temp2.value() & mask), valid, error };
			}

		case 7:
			{
				uint16_t calculated_address = get_register(reg) + next_word;
				temp2 = b->peek_word(run_mode, calculated_address);
				if (temp2.has_value() == false)
					temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", calculated_address);
				else {
					temp2 = b->peek_word(run_mode, temp2.value());
					if (temp2.has_value() == false)
						temp2 = 0xffff, valid = false, error = format("cannot fetch memory from %o", temp2.value());
				}

				if (reg == 7)
					return { format("@%06o", next_word), 4, int(next_word), uint16_t(temp2.value() & mask), valid, error };

				return { format("@%o(%s)", next_word, reg_name.c_str()), 4, int(next_word), uint16_t(temp2.value() & mask), valid, error };
			}
	}

	operand_parameters out { };
	out.error  = format("unknown register mode %d", mode);
	return out;
}

// col 0: src = 0, dst = 0
// col 1: src = 1-7, dst = 0
// col 2: src = 0-7, dst = 1-7
uint32_t timings_double_operand(const int col0, const int col1, const int col2, int col2c, const int src, const int dst, const int dst_reg)
{
	if (src == 0 && dst == 0)
		return col0 + (dst_reg == 7 ? 300 : 0);

	if (dst == 0)
		return col1 + (dst_reg == 7 ? 300 : 0);

	return col2 + (src != 0 && dst >= 6 ? col2c : 0);
}

uint16_t cpu::peek_dst(const int mode, const int reg, const uint16_t pc, const word_mode_t word_mode) const
{
	int         run_mode  = getPSW_runmode();
	auto        temp      = b->peek_word(run_mode, pc);
	if (temp.has_value() == false)
		return 0;
	uint16_t    next_word = temp.value();
	uint16_t    mask      = word_mode_mask[word_mode];

	std::optional<uint16_t> temp2;

	switch(mode) {
		case 0:
			return get_register(reg) & mask;

		case 1:
			temp2 = b->peek_word(run_mode, get_register(reg));
			if (temp2.has_value() == false)
				return 0;

			return temp2.value() & mask;

		case 2:
			if (reg == 7)
				return next_word & mask;

			temp2 = b->peek_word(run_mode, get_register(reg));
			if (temp2.has_value() == false)
				temp2 = 0;

			return temp2.value() & mask;

		case 3:
			if (reg == 7) {
				temp2 = b->peek_word(run_mode, next_word);
				if (temp2.has_value() == false)
					return 0;

				return temp2.value() & mask;
			}

			temp2 = b->peek_word(run_mode, get_register(reg));
			if (temp2.has_value() == false)
				return 0;

			temp2 = b->peek_word(run_mode, temp2.value());
			if (temp2.has_value() == false)
				return 0;

			return temp2.value() & mask;

		case 4:
			temp2 = b->peek_word(run_mode, get_register(reg) - (word_mode == wm_word || reg >= 6 ? 2 : 1));
			if (temp2.has_value() == false)
				return 0;

			return temp2.value() & mask;

		case 5:
			temp2 = b->peek_word(run_mode, get_register(reg) - 2);
			if (temp2.has_value() == false)
				return 0;

			temp2 = b->peek_word(run_mode, temp2.value());
			if (temp2.has_value() == false)
				return 0;

			return temp2.value() & mask;

		case 6:
			temp2 = b->peek_word(run_mode, get_register(reg) + next_word);
			if (temp2.has_value() == false)
				return 0;

			return temp2.value() & mask;

		case 7:
			temp2 = b->peek_word(run_mode, get_register(reg) + next_word);
			if (temp2.has_value() == false)
				return 0;

			temp2 = b->peek_word(run_mode, temp2.value());
			if (temp2.has_value() == false)
				return 0;

			return temp2.value() & mask;
	}

	assert(false);
	return 0;
}

uint32_t cpu::calc_instruction_duration(const uint16_t pc) const
{
	constexpr const uint32_t mtp_dst[] { 900, 1650, 1650, 2100, 1800, 2250, 2100, 2550 };
	constexpr const uint32_t srcdst_timings[] { 0, 300, 300, 750, 450, 900, 600, 1050 };  // mode
	constexpr const uint32_t jmp_dst[] { 0, 1950, 1950, 2250, 1950, 2400, 2100, 2550 };
	uint32_t                 src_time = 0;
	uint32_t                 ef_time  = 0;
	uint32_t                 dst_time = 0;

	int         run_mode    = getPSW_runmode();
	auto        temp        = b->peek_word(run_mode, pc);
	if (temp.has_value() == false)
		return 0;
	uint16_t    instruction = temp.value();
	word_mode_t word_mode   = instruction & 0x8000 ? wm_byte : wm_word;
	int         src         = (instruction >> 9) & 7;
	int         src_reg     = (instruction >> 6) & 7;
	int         dst         = (instruction >> 3) & 7;
	int         dst_reg     =  instruction       & 7;
	uint16_t    work_val    = 0;

	switch(instruction >> 12) {
		case 0:
			switch((instruction >> 6) & 077) {
				case 000:
					switch(instruction) {
						case 0:  // HALT
							ef_time = 1050;
							break;
						case 1:  // WAIT
							ef_time = 450;
							break;
						case 2:  // RTI
						case 6:  // RTT
							ef_time = 1500;
							break;
						case 5:  // RESET
							ef_time = 10000000;  // 10 ms
							break;
						case 4:  // IOT
							ef_time = 3300;
							break;
						case 7:  // MFP
							ef_time = 1500;
							break;
						default:
							DOLOG(warning, false, "DEFAULT group 1 0/%o -> %06o", (instruction >> 6) & 077, instruction);
							break;
					}
					break;
				case 001:  // JMP
					ef_time = jmp_dst[dst];
					break;
				case 002:  // RTS
					ef_time = 1050;
					break;
				case 040:
				case 041:
				case 042:
				case 043:
				case 044:
				case 045:
				case 046:
				case 047:  // JSR
					ef_time = jmp_dst[dst];
					break;
				case 054:  // NEG
					ef_time = dst == 0 ? 750 : 1500;
					break;
				case 003:  // SWAB
				case 050:  // CLR
				case 051:  // COM
				case 052:  // INC
				case 053:  // DEC
				case 055:  // ADC
				case 056:  // SBC
				case 061:  // ROL
				case 063:  // ASL
				case 067:  // SXT
					ef_time = dst == 0 ? 300 : 1200;
					ef_time += dst == 0 && dst_reg == 7 ? 300 : 0;
					break;
				case 057:  // TST
					ef_time = dst == 0 ? 300 : 450;
					ef_time += dst == 0 && dst_reg == 7 ? 300 : 0;
					break;
				case 060:  // ROR
				case 062:  // ASR
					ef_time = dst == 0 ? 300 : 1200;
					ef_time += dst == 0 && dst_reg == 7 ? 300 : 0;
					work_val = peek_dst(dst, dst_reg, pc + 2, word_mode);
					ef_time += work_val & 1 ? 150 : 0;
					break;
				case 064:  // MARK
					ef_time = 900;
					break;
				case 065:  // MFPI
					ef_time = 1500;
					break;
				case 066:  // MTPI
					ef_time = mtp_dst[dst];
					break;
				default: {
						 auto rc = conditional_branch_instructions_evaluate(instruction);
						 if (rc.has_value()) {
							 ef_time = rc.value() ? 600 : 300;
							 break;
						 }
					 }
					 DOLOG(warning, false, "DEFAULT group 2 %o -> %06o", (instruction >> 6) & 077, instruction);
					 break;
			}
			break;

		case 011:   // MOVB
		case 1: {  // MOV
				src_time = srcdst_timings[src];

				if (word_mode == wm_word) {
					switch(dst) {
						case 0:
							if (dst_reg == 7)
								ef_time = src == 0 ? 300 : 450;
							else  // 0-6
								ef_time = src == 0 ? 600 : 750;
							break;
						case 1:
						case 2:
							ef_time = 1200;
							break;
						case 3:
							ef_time = 1650;
							break;
						case 4:
							ef_time = 1350;
							break;
						case 5:
							ef_time = 1800;
							break;
						case 6:
							ef_time = src == 0 ? 1500 : 1650;
							break;
						case 7:
							ef_time = src == 0 ? 1950 : 2100;
							break;
						default:
							DOLOG(warning, false, "DEFAULT group 3");
							break;
					}
				}
				else {
					ef_time  = timings_double_operand(300, 450, 1200, 150, src, dst, dst_reg);
					dst_time = srcdst_timings[dst];
				}
			}
			break;
		case 2:  // CMP
		case 012:  // CMPB
		case 3:  // BIT
		case 013:  // BITB
			ef_time  = timings_double_operand(300, 450, 450, 150, src, dst, dst_reg);
			src_time = srcdst_timings[src];
			dst_time = srcdst_timings[dst];
			break;
		case 4:  // BIC
		case 014:  // BICB
		case 5:  // BIS
		case 015:  // BISB
		case 6:  // ADD
		case 016:  // SUB
			ef_time  = timings_double_operand(300, 450, 1200, 150, src, dst, dst_reg);
			src_time = srcdst_timings[src];
			dst_time = srcdst_timings[dst];
			break;
		case 7:
			work_val = peek_dst(dst, dst_reg, pc + 2, word_mode);

			switch((instruction >> 9) & 7) {
				case 0:  // MUL
					ef_time  = 3300;
					src_time = srcdst_timings[src];
					break;

				case 1:  // DIV
					if (work_val == 0)
						ef_time = 900;
					else
						ef_time = 7050 + work_val * 150 / 65535;
					break;

				case 2:  // ASH
				case 3:  // ASHC
					dst_time = srcdst_timings[dst];
					ef_time  = (dst == 0 ? 750 : 900) + 150 * (work_val & 077);
					break;

				case 4:  // XOR
					ef_time  = timings_double_operand(300, 300, 1200, 0, src, dst, dst_reg);
					src_time = srcdst_timings[src];
					dst_time = srcdst_timings[dst];
					break;

				case 7:  // SOB
					ef_time  = lowlevel_register_get(get_register_set(), src_reg) >= 1 ? 600 : 750;  // branch is faster
					break;
				default:
					DOLOG(warning, false, "DEFAULT group 4");
					break;
			}
			break;
		case 010:
			{  // branch
				auto rc = conditional_branch_instructions_evaluate(instruction);
				if (rc.has_value()) {
					ef_time = rc.value() ? 600 : 300;
					break;
				}
			}

			switch((instruction >> 6) & 077) {
				case 040:
				case 041:
				case 042:
				case 043:  // EMT
					  ef_time = 3300;
					  break;
				case 044:
				case 045:
				case 046:
				case 047:  // TRAP
					  ef_time = 600;
					  break;
				case 050:  // CLRB
				case 051:  // COMB
				case 052:  // INCB
				case 053:  // DECB
				case 055:  // ADCB
				case 056:  // SBCB
				case 061:  // ROLB
				case 063:  // ASLB
					ef_time = dst == 0 ? 300 : 1200;
					ef_time += dst == 0 && dst_reg == 7 ? 300 : 0;
					break;
				case 054:  // NEGB
					ef_time = dst == 0 ? 750 : 1500;
					break;
				case 057:  // TSTB
					ef_time = dst == 0 ? 300 : 450;
					ef_time += dst == 0 && dst_reg == 7 ? 300 : 0;
					break;
				case 060:  // RORB
				case 062:  // ASRB
					ef_time = dst == 0 ? 300 : 1200;
					ef_time += dst == 0 && dst_reg == 7 ? 300 : 0;
					work_val = peek_dst(dst, dst_reg, pc + 2, word_mode);
					ef_time += work_val & 1 ? 150 : 0;
					break;
				default:
					DOLOG(warning, false, "DEFAULT group 5");
					break;
			}
			break;
		case 017:
			// FPP
			break;
		default:
			DOLOG(warning, false, "DEFAULT group 6");
			break;
	}

	uint32_t result = src_time + ef_time + dst_time;
	if (result == 0)
		DOLOG(debug, false, "%06o @ %06o: unspecified duration", instruction, pc);
	return result;
}

std::unordered_map<std::string, std::vector<std::string> > cpu::disassemble(const uint16_t addr) const
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
	bool                  might_be_io = false;  // is this instruction working on io

	if (do_opcode == 0b000) {
		auto addressing = addressing_to_string(dst_register, addr + 2, word_mode);
		might_be_io = addressing.valid == false;
		auto dst_text { addressing };

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
				else
					name = "MARK";
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

		if (text.empty() == false && dst_text.valid == false)
			text += " (INV1)";

		if (text.empty() == false && next_word != -1)
			instruction_words.push_back(next_word);
	}
	else if (do_opcode == 0b111) {
		if (word_mode == wm_byte)
			name = "?";
		else {
			std::string src_text = format("R%d", (instruction >> 6) & 7);
			auto        addressing = addressing_to_string(dst_register, addr + 2, word_mode);
			might_be_io = addressing.valid == false;
			auto        dst_text { addressing };

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
		auto addressing_src = addressing_to_string(src_register, addr + 2, word_mode);
		might_be_io = addressing_src.valid == false;
		auto src_text { addressing_src };

		auto next_word_src = src_text.instruction_part;
		if (next_word_src != -1)
			instruction_words.push_back(next_word_src);

		work_values.push_back(src_text.work_value);

		// destination
		auto addressing_dst = addressing_to_string(dst_register, addr + src_text.length, word_mode);
		might_be_io = addressing_dst.valid == false;
		auto dst_text { addressing_dst };

		auto next_word_dst = dst_text.instruction_part;
		if (next_word_dst != -1)
			instruction_words.push_back(next_word_dst);

		work_values.push_back(dst_text.work_value);

		if (src_text.valid == false || dst_text.valid == false)
			text += " (INV3)";

		if (do_opcode == 0b110)
			word_mode_str.clear();
		text = name + word_mode_str + space + src_text.operand + comma + dst_text.operand;
	}

	if (text.empty()) {  // conditional branch instructions
		uint8_t  cb_opcode = instruction >> 8;
		int8_t   offset    = instruction;
		uint16_t new_pc    = addr + 2 + offset * 2;

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
			text = name + space + format("0%06o", new_pc);
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
			auto addressing = addressing_to_string(dst_register, addr + 2, word_mode);
			might_be_io = addressing.valid == false;
			auto dst_text { addressing };

			auto next_word = dst_text.instruction_part;
			if (next_word != -1)
				instruction_words.push_back(next_word);

			work_values.push_back(dst_text.work_value);

			text = std::string("JMP ") + dst_text.operand;

			if (dst_text.valid == false)
				text += " (INV4)";

			if (addressing.error.has_value())
				text += " " + addressing.error.value();
		}

		if ((instruction & 0b1111111000000000) == 0b0000100000000000) {
			auto addressing = addressing_to_string(dst_register, addr + 2, word_mode);
			might_be_io = addressing.valid == false;
			auto dst_text { addressing };

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

	std::unordered_map<std::string, std::vector<std::string> > out;

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

	out.insert({ "MMR0", { format("%06o", mmu_->getMMR0()) } });
	out.insert({ "MMR1", { format("%06o", mmu_->getMMR1()) } });
	out.insert({ "MMR2", { format("%06o", mmu_->getMMR2()) } });
	out.insert({ "MMR3", { format("%06o", mmu_->getMMR3()) } });

	out.insert({ "duration", { format("%u", calc_instruction_duration(addr)) } });

	out.insert({ "works-on-io", { format("%u", might_be_io) } });

	return out;
}

bool cpu::step()
{
	it_is_a_trap = false;

#if defined(TEENSY4_1)
	if (any_queued_interrupts)
		execute_any_pending_interrupt();
#else
	if (any_queued_interrupts.load(std::memory_order_relaxed))
		execute_any_pending_interrupt();
#endif

	try {
		instruction_start = getPC();

		if (!mmu_->isMMR1Locked())
			mmu_->setMMR2(instruction_start);

		uint16_t instr = b->read_word(instruction_start);
		add_register(7, 2);

		if (double_operand_instructions(instr) || conditional_branch_instructions(instr) || condition_code_operations(instr) || misc_operations(instr)) {
			if (!mmu_->isMMR1Locked())
				mmu_->clearMMR1();

			if (delayed_trap.has_value()) {
				trap(delayed_trap.value(), 7);
				delayed_trap.reset();
			}

			return true;
		}

		DOLOG(debug, false, "UNHANDLED instruction %06o @ %06o", instr, instruction_start);

		trap(010);  // floating point nog niet geimplementeerd

		return false;
	}
	catch(const int exception_nr) {
		TRACE("trap during execution of command (%d)", exception_nr);
	}

	return true;
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
        j["stack_limit_register"]  = stack_limit_register;
        j["processing_trap_depth"] = processing_trap_depth;
        j["it_is_a_trap"]          = it_is_a_trap;
        j["debug_mode"]            = debug_mode;

	if (delayed_trap.has_value())
		j["delayed_trap"] = delayed_trap.value();

	JsonVariant j_queued_interrupts;
	for(int il=0; il<8; il++) {
		JsonDocument ja_qi_level;
		JsonArray ja_qi_level_work = ja_qi_level.to<JsonArray>();
		for(auto v: queued_interrupts[il])
			ja_qi_level_work.add(v);

		j_queued_interrupts[format("%d", il)] = ja_qi_level;
	}

	j["queued_interrupts"]     = j_queued_interrupts;

	j["any_queued_interrupts"] = bool(any_queued_interrupts);

	return j;
}

cpu *cpu::deserialize(const JsonVariantConst j, bus *const b, kek_event_t *const event)
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
        c->stack_limit_register  = j["stack_limit_register"];
        c->processing_trap_depth = j["processing_trap_depth"];
        c->it_is_a_trap          = j["it_is_a_trap"];
        c->debug_mode            = j["debug_mode"];

	if (j.containsKey("delayed_trap"))
		c->delayed_trap  = j["delayed_trap"];
	else
		c->delayed_trap.reset();

	c->any_queued_interrupts = j["any_queued_interrupts"].as<bool>();

	c->init_interrupt_queue();
	for(int level=0; level<8; level++) {
		JsonArrayConst ja_qi_level = j["queued_interrupts"][format("%d", level)].as<JsonArrayConst>();
		for(auto v : ja_qi_level)
			c->queued_interrupts[level].insert(v.as<int>());
	}

	return c;
}
