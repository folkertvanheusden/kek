#include <optional>
#include <string>
#include <vector>

#include "breakpoint.h"


class breakpoint_or : public breakpoint
{
private:
	const std::vector<breakpoint *> triggers;

public:
	breakpoint_or(bus *const b, const std::vector<breakpoint *> & triggers);
	virtual ~breakpoint_or();

	virtual std::optional<std::string> is_triggered() const override;

	virtual std::string emit() const override;
};
