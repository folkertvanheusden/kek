#include "console.h"


console::console(std::atomic_bool *const terminate) :
	terminate(terminate)
{
	th = new std::thread(std::ref(*this));
}

console::~console()
{
	th->join();

	delete th;
}

bool console::poll_char()
{
	return buffer.empty() == false;
}

uint8_t console::get_char()
{
	if (buffer.empty())
		return 0x00;

	char c = buffer.at(0);

	buffer.erase(buffer.begin() + 0);

	return c;
}

void console::operator()()
{
	while(!*terminate) {
		int c = wait_for_char(500);

		if (c == -1)
			continue;

		if (c == 3)
			*terminate = true;
		else
			buffer.push_back(c);
	}
}
