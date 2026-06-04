// (C) 2024-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <Arduino.h>
#include "comm.h"
#include "my_lock.h"


class comm_arduino: public comm
{
private:
	Stream *const s    { nullptr };
	std::string   name;
	my_lock       lock;

public:
	comm_arduino(Stream *const s, const std::string & name);
	virtual ~comm_arduino();

	bool    begin() override;

#if IS_POSIX
	JsonDocument serialize() const override;
	static comm_arduino *deserialize(const JsonVariantConst j);
#endif

	std::string get_identifier() const;

	bool    is_connected() override;

	bool    has_data() override;
	uint8_t get_byte() override;

	void    send_data(const uint8_t *const in, const size_t n) override;
};
