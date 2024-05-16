// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include "comm.h"


class comm_posix_tty: public comm
{
private:
	int fd { -1 };

public:
	comm_posix_tty(const std::string & dev, const int bitrate);
	virtual ~comm_posix_tty();

	bool    is_connected() override;

	bool    has_data() override;
	uint8_t get_byte() override;

	void    send_data(const uint8_t *const in, const size_t n) override;
};
