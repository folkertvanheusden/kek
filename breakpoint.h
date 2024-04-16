#pragma once

#include <optional>
#include <string>

#include "bus.h"


class breakpoint
{
protected:
	bus *const b { nullptr };

public:
	breakpoint(bus *const b);
	virtual ~breakpoint();

	virtual std::optional<std::string> is_triggered() const = 0;

	virtual std::string emit() const = 0;
};
