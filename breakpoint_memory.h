#include <optional>
#include <set>
#include <string>

#include "breakpoint.h"
#include "bus.h"


class breakpoint_memory : public breakpoint
{
private:
	const uint32_t     addr       { 0       };
	const word_mode_t  word_mode  { wm_word };
	const bool         is_virtual { false   };
	std::set<uint16_t> values;

public:
	breakpoint_memory(bus *const b, const uint32_t addr, const word_mode_t word_mode, const bool is_virtual, const std::set<uint16_t> & values);
	virtual ~breakpoint_memory();

	virtual std::optional<std::string> is_triggered() const override;

	static std::pair<breakpoint_memory *, std::optional<std::string> > parse(bus *const b, const std::string & in);

	virtual std::string emit() const override;
};
