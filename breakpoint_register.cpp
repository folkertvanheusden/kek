#include <ctype.h>

#include "breakpoint_register.h"
#include "cpu.h"
#include "utils.h"


breakpoint_register::breakpoint_register(bus *const b, const int register_nr, const std::set<uint16_t> & values) :
	breakpoint(b),
	c(b->getCpu()),
	register_nr(register_nr), values(values)
{
}

breakpoint_register::~breakpoint_register()
{
}

std::optional<std::string> breakpoint_register::is_triggered() const
{
	uint16_t v  = c->getRegister(register_nr);  // TODO run-mode

	auto     it = values.find(v);
	if (it == values.end())
		return { };

	return format("R%d=%06o", register_nr, v);
}

std::pair<breakpoint_register *, std::optional<std::string> > breakpoint_register::parse(bus *const b, const std::string & in)
{
	auto parts = split(in, "=");
	if (parts.size() != 2)
		return { nullptr, "register: key or value missing" };

	auto values_in = parts.at(1);
	auto v_parts = split(values_in, ",");
	std::set<uint16_t> values;
	for(auto & v: v_parts)
		values.insert(std::stoi(v, nullptr, 8));

	auto key = parts.at(0);
	if (key.size() < 2)
		return { nullptr, "register: register id invalid" };

	if (toupper(key[0]) == 'R') {
		int nr = key[1] - '0';
		if (nr < 0 || nr > 7)
			return { nullptr, "register: register id invalid" };

		return { new breakpoint_register(b, nr, values), { } };
	}
	else if (key == "SP" || key == "sp") {
		return { new breakpoint_register(b, 6, values), { } };
	}
	else if (key == "PC" || key == "pc") {
		return { new breakpoint_register(b, 7, values), { } };
	}

	return { nullptr, "register: invalid specification" };
}

std::string breakpoint_register::emit() const
{
	std::string out;

	for(auto & v: values) {
		if (out.empty())
			out = format("R%d", register_nr) + "=";
		else
			out += ",";

		out += format("%06o", v);
	}

	return out;
}
