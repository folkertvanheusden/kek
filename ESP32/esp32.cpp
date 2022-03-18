#include <Arduino.h>

std::string read_terminal_line(const std::string & prompt)
{
	Serial.print(prompt.c_str());
	Serial.print(F(">"));

	std::string str;

	for(;;) {
		if (Serial.available()) {
			char c = Serial.read();

			if (c == 13 || c == 10)
				break;

			if (c >= 32 && c < 127) {
				str += c;

				Serial.print(c);
			}
		}
	}

	Serial.println(F(""));

	return str;
}
