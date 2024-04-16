#include <optional>
#include <string>

#include "breakpoint.h"
#include "breakpoint_and.h"
#include "breakpoint_memory.h"
#include "breakpoint_or.h"
#include "breakpoint_register.h"
#include "bus.h"
#include "utils.h"


static void delete_parsed(const std::vector<breakpoint *> & parsed)
{
	for(auto & p: parsed)
		delete p;
}

std::pair<breakpoint *, std::optional<std::string> > parse_breakpoint(bus *const b, const std::string & in)
{
	auto parts = split(in, " ");

	std::vector<breakpoint *> parsed;

	enum { combine_not_set, combine_single, combine_and, combine_or } combine { combine_not_set };

	for(size_t i=0; i<parts.size(); i++) {
		std::pair<breakpoint *, std::optional<std::string> > current;

		if (parts[i][0] == '(') {
			int depth = 0;

			std::optional<size_t> end_index;
			for(size_t j=i; j<parts.size(); j++) {
				if (parts[j][0] == '(')
					depth++;

				for(size_t count=0; count<parts[j].size(); count++) {
					if (parts[j][count] == ')')
						depth--;
				}

				if (depth == 0) {
					end_index = j;
					break;
				}
			}

			if (depth != 0) {
				delete_parsed(parsed);
				return { nullptr, "( and ) unbalanced: " + in };
			}

			std::string temp;
			for(size_t j=i; j<=end_index.value(); j++)
				temp += (j > i ? " " : "") + parts.at(j);

			auto rc = parse_breakpoint(b, temp.substr(1, temp.size() - 2));
			if (rc.first == nullptr) {
				delete_parsed(parsed);
				return rc;
			}

			i = end_index.value();

			parsed.push_back(rc.first);
		}
		else {
			if (parts[i] == "and" || parts[i] == "or") {
				if ((combine == combine_and && parts[i] == "or") || (combine == combine_or && parts[i] == "and")) {
					delete_parsed(parsed);
					return { nullptr, "combining and/or in one definition" };
				}
				combine = parts[i] == "and" ? combine_and : combine_or;
				continue;
			}
			else if (combine == combine_single) {
				delete_parsed(parsed);
				return { nullptr, "and/or missing" };
			}
			else {
				if (combine == combine_not_set)
					combine = combine_single;

				auto rc_reg = breakpoint_register::parse(b, parts[i]);
				if (rc_reg.first == nullptr && rc_reg.second.has_value()) {
					delete_parsed(parsed);
					return { nullptr, "not understood: " + rc_reg.second.value() };
				}

				if (rc_reg.first)
					parsed.push_back(rc_reg.first);

				auto rc_mem = breakpoint_memory::parse(b, parts[i]);
				if (rc_mem.first == nullptr && rc_mem.second.has_value()) {
					delete_parsed(parsed);
					return { nullptr, "not understood: " + rc_mem.second.value() };
				}

				if (rc_mem.first)
					parsed.push_back(rc_mem.first);
			}
		}
	}

	if (combine == combine_and)
		return { new breakpoint_and(b, parsed), {  } };

	if (combine == combine_or)
		return { new breakpoint_or(b, parsed), {  } };

	if (parsed.size() != 1) {
		delete_parsed(parsed);
		return { nullptr, "wrong count of items" };
	}

	return { parsed.at(0), { } };
}
