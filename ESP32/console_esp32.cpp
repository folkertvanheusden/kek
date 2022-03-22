#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "console_esp32.h"
#include "error.h"


console_esp32::console_esp32(std::atomic_bool *const terminate) : console(terminate)
{
}

console_esp32::~console_esp32()
{
}

int console_esp32::wait_for_char(const int timeout)
{
	for(int i=0; i<timeout / 10; i++) {
		if (Serial.available())
			return Serial.read();

		delay(10);
	}

	return -1;
}

void console_esp32::put_char(const char c)
{
	Serial.print(c);
}

void console_esp32::resize_terminal()
{
}
