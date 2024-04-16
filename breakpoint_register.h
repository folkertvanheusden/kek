#include <optional>
#include <set>
#include <string>

#include "breakpoint.h"
#include "bus.h"


class breakpoint_register : public breakpoint
{
private:
	cpu         *const c           { nullptr };
	int                register_nr { -1      };
	std::set<uint16_t> values;

public:
	breakpoint_register(bus *const b, const int register_nr, const std::set<uint16_t> & values);
	virtual ~breakpoint_register();

	virtual std::optional<std::string> is_triggered() const override;

	static std::pair<breakpoint_register *, std::optional<std::string> > parse(bus *const b, const std::string & in);

	virtual std::string emit() const override;
};
