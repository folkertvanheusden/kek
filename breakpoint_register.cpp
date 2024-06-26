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

std::string breakpoint_register::get_name(hwreg_t reg) const
{
	if (reg < 8)
		return format("R%d", reg);

	switch(reg) {
		case hr_mmr0:
			return "mmr0";
		case hr_mmr1:
			return "mmr1";
		case hr_mmr2:
			return "mmr2";
		case hr_mmr3:
			return "mmr3";
		case hr_psw:
			return "psw";
	}

	return "???";
}

std::optional<std::string> breakpoint_register::is_triggered() const
{
	uint16_t v  = 0;

	if (register_nr < 8)
		v = c->get_register(register_nr);  // TODO run-mode
	else {
		hwreg_t reg = hwreg_t(register_nr);

		switch(reg) {
			case hr_mmr0:
				v = b->getMMU()->getMMR0();
				break;
			case hr_mmr1:
				v = b->getMMU()->getMMR1();
				break;
			case hr_mmr2:
				v = b->getMMU()->getMMR2();
				break;
			case hr_mmr3:
				v = b->getMMU()->getMMR3();
				break;
			case hr_psw:
				v = c->getPSW();
				break;
		}
	}

	auto     it = values.find(v);
	if (it == values.end())
		return { };

	return get_name(hwreg_t(register_nr)) + "=" + format("%06o", v);
}

std::pair<breakpoint_register *, std::optional<std::string> > breakpoint_register::parse(bus *const b, const std::string & in)
{
	auto parts = split(in, "=");
	if (parts.size() != 2)
		return { nullptr, "register: key or value missing" };

	auto values_in = parts.at(1);
	auto v_parts = split(std::move(values_in), ",");
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
	else if (key.substr(0, 3) == "MMR" || key.substr(0, 3) == "mmr") {
		int which = key[3] - '0';

		return { new breakpoint_register(b, hr_mmr0 + which, values), { } };
	}
	else if (key.substr(0, 3) == "PSW" || key.substr(0, 3) == "psw") {
		return { new breakpoint_register(b, hr_psw, values), { } };
	}

	return { nullptr, { } };
}

std::string breakpoint_register::emit() const
{
	std::string out;

	for(auto & v: values) {
		if (out.empty())
			out = get_name(hwreg_t(register_nr)) + "=";
		else
			out += ",";

		out += format("%06o", v);
	}

	return out;
}
