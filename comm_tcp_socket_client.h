// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include "comm.h"

#if defined(_WIN32)
#include <ws2tcpip.h>
#include <winsock2.h>
#else
#define SOCKET int
#define INVALID_SOCKET -1
#endif


class comm_tcp_socket_client: public comm
{
private:
	const std::string host;
	const int         port      { -1             };
	std::atomic_bool  stop_flag { false          };
	SOCKET            cfd       { INVALID_SOCKET };
        std::mutex        cfd_lock;
	std::thread      *th        { nullptr        };

public:
	comm_tcp_socket_client(const std::string & host, const int port);
	virtual ~comm_tcp_socket_client();

	bool    is_connected() override;

	bool    has_data() override;
	uint8_t get_byte() override;

	void    send_data(const uint8_t *const in, const size_t n) override;

	void    operator()();
};
