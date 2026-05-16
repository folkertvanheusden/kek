#include <ctype.h>

#include "breakpoint_instruction.h"
#include "cpu.h"
#include "utils.h"


breakpoint_instruction::breakpoint_instruction(bus *const b, const std::pair<uint16_t, uint16_t> instr_mask, const bp_action action):
	breakpoint(b, action),
	instruction(instr_mask.first),
	mask(instr_mask.second)
{
}

breakpoint_instruction::~breakpoint_instruction()
{
}

std::optional<std::string> breakpoint_instruction::is_triggered() const
{
	uint16_t cur_pc    = b->getCpu()->getPC();
	auto     cur_instr = b->peek_word(b->getCpu()->getPSW_runmode(), cur_pc);
	if (cur_instr.has_value() == false)
		return { };

	if ((cur_instr.value() & mask) == instruction)
		return format("instr[%06o]=%06o", mask, cur_instr.value());

	return { };
}

std::pair<breakpoint_instruction *, std::optional<std::string> > breakpoint_instruction::parse(bus *const b, const std::string & in, const bp_action action)
{
	auto parts = split(in, "=");
	if (parts.size() != 2)
		return { nullptr, "instruction: key or value missing" };

	uint16_t instruction = std::stoi(parts.at(1), nullptr, 8);

	auto key = parts.at(0);
	if (key.size() < 7 || (key.substr(0, 5) != "INSTR" && key.substr(0, 5) != "instr"))
		return { nullptr, { } };

	uint16_t    mask     = 65535;
	int         len      = key.size() - (6 + 1);
	std::string mask_str = len > 0 ? key.substr(6, len) : "";
	if (mask_str.empty() == false)
		mask = std::stoi(mask_str, nullptr, 8);

	return { new breakpoint_instruction(b, { instruction, mask }, action), { } };
}

std::string breakpoint_instruction::emit() const
{
	return format("instr[%06o]=%06o", mask, instruction);
}
