#include "breakpoint_or.h"
#include "utils.h"


breakpoint_or::breakpoint_or(bus *const b, const std::vector<breakpoint *> & triggers):
	breakpoint(b),
	triggers(triggers)
{
}

breakpoint_or::~breakpoint_or()
{
	for(auto & bp: triggers)
		delete bp;
}

std::optional<std::string> breakpoint_or::is_triggered() const
{
	for(auto & t: triggers) {
		auto rc = t->is_triggered();
		if (rc.has_value())
			return rc;
	}

	return { };
}

std::string breakpoint_or::emit() const
{
	std::string out;

	for(auto & t: triggers) {
		if (out.empty())
			out = "(";
		else
			out += " or ";

		out += t->emit();
	}

	out += ")";

	return out;
}
