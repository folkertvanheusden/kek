// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include "comm.h"

#if defined(_WIN32)
#include <ws2tcpip.h>
#include <winsock2.h>
#else
#define SOCKET int
#define INVALID_SOCKET -1
#endif


class comm_tcp_socket: public comm
{
private:
	SOCKET fd { INVALID_SOCKET };

public:
	comm_tcp_socket(const int port);
	virtual ~comm_tcp_socket();

	virtual bool    is_connected() = 0;

	virtual bool    has_data() = 0;
	virtual uint8_t get_byte() = 0;

	virtual void    send_data(const uint8_t *const in, const size_t n) = 0;
};