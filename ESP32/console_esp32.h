#include <Arduino.h>

#include "console.h"


class console_esp32 : public console
{
protected:
	int wait_for_char(const short timeout) override;

	void put_char_ll(const char c) override;

public:
	console_esp32(std::atomic_bool *const terminate, bus *const b);
	virtual ~console_esp32();

	void resize_terminal() override;

	void refresh_virtual_terminal() override;

	void panel_update_thread() override;
};
