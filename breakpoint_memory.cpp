#include <ctype.h>

#include "breakpoint_memory.h"
#include "cpu.h"
#include "utils.h"


breakpoint_memory::breakpoint_memory(bus *const b, const uint32_t addr, const word_mode_t word_mode, const bool is_virtual, const std::set<uint16_t> & values):
	breakpoint(b),
	addr(addr),
	word_mode(word_mode),
	is_virtual(is_virtual),
	values(values)
{
}

breakpoint_memory::~breakpoint_memory()
{
}

std::optional<std::string> breakpoint_memory::is_triggered() const
{
	uint16_t v  = 0;

	if (is_virtual)
		v = b->read(addr, word_mode, rm_cur, true, i_space);
	else
		v = b->readPhysical(addr);

	auto     it = values.find(v);
	if (it == values.end())
		return { };

	return format("MEM%c%c[%08o]=%06o", word_mode == wm_byte ? 'B' : 'W', is_virtual ? 'V' : 'P', addr, v);
}

std::pair<breakpoint_memory *, std::optional<std::string> > breakpoint_memory::parse(bus *const b, const std::string & in)
{
	auto parts = split(in, "=");
	if (parts.size() != 2)
		return { nullptr, "memory: key or value missing" };

	auto values_in = parts.at(1);
	auto v_parts = split(std::move(values_in), ",");
	std::set<uint16_t> values;
	for(auto & v: v_parts)
		values.insert(std::stoi(v, nullptr, 8));

	auto key = parts.at(0);
	if (key.size() < 8 || (key.substr(0, 3) != "MEM" && key.substr(0, 3) != "mem"))
		return { nullptr, { } };

	word_mode_t wm         = toupper(key[3]) == 'B' ? wm_byte : wm_word;
	bool        is_virtual = toupper(key[4]) == 'V';

	std::size_t end_marker = key.find(']');
	uint32_t    addr       = std::stoi(key.substr(6, end_marker - 6), nullptr, 8);

	return { new breakpoint_memory(b, addr, wm, is_virtual, values), { } };
}

std::string breakpoint_memory::emit() const
{
	std::string out;

	for(auto & v: values) {
		if (out.empty())
			out = format("MEM%c%c[%08o]=", word_mode == wm_byte ? 'B' : 'W', is_virtual ? 'V' : 'P', addr);
		else
			out += ",";

		out += format("%06o", v);
	}

	return out;
}
