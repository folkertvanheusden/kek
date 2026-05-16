#include <optional>
#include <set>
#include <string>

#include "breakpoint.h"
#include "bus.h"


class breakpoint_instruction : public breakpoint
{
private:
	const uint16_t instruction { 0 };
	const uint16_t mask        { 0 };

public:
	breakpoint_instruction(bus *const b, const std::pair<uint16_t, uint16_t> instr_mask, const bp_action action);
	virtual ~breakpoint_instruction();

	virtual std::optional<std::string> is_triggered() const override;

	static std::pair<breakpoint_instruction *, std::optional<std::string> > parse(bus *const b, const std::string & in, const bp_action action);

	virtual std::string emit() const override;
};
