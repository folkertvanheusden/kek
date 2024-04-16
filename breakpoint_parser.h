#include <optional>
#include <string>

#include "breakpoint.h"
#include "bus.h"


std::pair<breakpoint *, std::optional<std::string> > parse_breakpoint(bus *const b, const std::string & in);
