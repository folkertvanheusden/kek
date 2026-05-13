#include "breakpoint.h"


breakpoint::breakpoint(bus *const b, const bp_action action):
	b(b),
	action(action)
{
}

breakpoint::~breakpoint()
{
}
