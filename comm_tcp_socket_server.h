// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <atomic>
#include <mutex>
#include <thread>
#include "comm.h"

#if defined(_WIN32)
#include <ws2tcpip.h>
#include <winsock2.h>
#else
#define SOCKET int
#define INVALID_SOCKET -1
#endif


class comm_tcp_socket_server: public comm
{
private:
	const int        port      { -1             };
	std::atomic_bool stop_flag { false          };
	SOCKET           fd        { INVALID_SOCKET };
	SOCKET           cfd       { INVALID_SOCKET };
        std::mutex       cfd_lock;
	std::thread     *th        { nullptr        };

public:
	comm_tcp_socket_server(const int port);
	virtual ~comm_tcp_socket_server();

	std::string get_identifier() const override { format(":%d", port) + " (server)"; }

	bool    is_connected() override;

	bool    has_data() override;
	uint8_t get_byte() override;

	void    send_data(const uint8_t *const in, const size_t n) override;

	void    operator()();
};
