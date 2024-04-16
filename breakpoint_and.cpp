#include "breakpoint_and.h"
#include "utils.h"


breakpoint_and::breakpoint_and(bus *const b, const std::vector<breakpoint *> & triggers):
	breakpoint(b),
	triggers(triggers)
{
}

breakpoint_and::~breakpoint_and()
{
	for(auto & bp: triggers)
		delete bp;
}

std::optional<std::string> breakpoint_and::is_triggered() const
{
	std::string out;

	for(auto & t: triggers) {
		auto rc = t->is_triggered();
		if (rc.has_value() == false)
			return { };

		out += (out.empty() ? "" : ", ") + rc.value();
	}

	return out;
}

std::string breakpoint_and::emit() const
{
	std::string out;

	for(auto & t: triggers) {
		if (out.empty())
			out = "(";
		else
			out += " and ";

		out += t->emit();
	}

	out += ")";

	return out;
}
