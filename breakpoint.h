#pragma once

#include <optional>
#include <string>

#include "bus.h"


class breakpoint
{
public:
	enum bp_action { stop_running, start_tracing, only_log_entry, invalid };

protected:
	bus             *const b      { nullptr };
	const bp_action        action { invalid };

public:
	breakpoint(bus *const b, const bp_action action);
	virtual ~breakpoint();

	virtual std::optional<std::string> is_triggered() const = 0;

	virtual std::string emit      () const = 0;
	virtual bp_action   get_action() const { return action; }
};
