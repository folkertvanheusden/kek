#include <Arduino.h>

#include "console.h"


class console_esp32 : public console
{
private:
	Stream & io_port;

protected:
	int wait_for_char_ll(const short timeout) override;

	void put_char_ll(const char c) override;

public:
	console_esp32(std::atomic_uint32_t *const stop_event, bus *const b, Stream & io_port);
	virtual ~console_esp32();

	void put_string_lf(const std::string & what) override;

	void resize_terminal() override;

	void refresh_virtual_terminal() override;

	void panel_update_thread() override;
};
