#include <optional>
#include <string>
#include <vector>

#include "breakpoint.h"


class breakpoint_and : public breakpoint
{
private:
	const std::vector<breakpoint *> triggers;

public:
	breakpoint_and(bus *const b, const std::vector<breakpoint *> & triggers);
	virtual ~breakpoint_and();

	virtual std::optional<std::string> is_triggered() const override;

	virtual std::string emit() const override;
};
