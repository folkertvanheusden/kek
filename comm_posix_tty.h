// (C) 2024-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#if !defined(_WIN32)
#include "comm.h"


class comm_posix_tty: public comm
{
private:
	const std::string device;
	const int         bitrate;
	int               fd { -1 };

public:
	comm_posix_tty(const std::string & dev, const int bitrate);
	virtual ~comm_posix_tty();

	bool    begin() override;

#if IS_POSIX
	JsonDocument serialize() const override;
	static comm_posix_tty *deserialize(const JsonVariantConst j);
#endif

	std::string get_identifier() const override { return device; }

	bool    is_connected() override;

	bool    has_data() override;
	uint8_t get_byte() override;

	void    send_data(const uint8_t *const in, const size_t n) override;
};
#endif
