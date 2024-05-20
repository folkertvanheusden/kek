// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <cassert>
#include <cstring>

#include "comm.h"
#if defined(ARDUINO)
#include "comm_arduino.h"
#endif
#if defined(ESP32)
#include "comm_esp32_hardwareserial.h"
#endif
#if IS_POSIX
#include "comm_posix_tty.h"
#endif
#include "comm_tcp_socket_client.h"
#include "comm_tcp_socket_server.h"
#include "log.h"


comm::comm()
{
}

comm::~comm()
{
}

void comm::println(const char *const s)
{
	send_data(reinterpret_cast<const uint8_t *>(s), strlen(s));
	send_data(reinterpret_cast<const uint8_t *>("\r\n"), 2);
}

void comm::println(const std::string & in)
{
	send_data(reinterpret_cast<const uint8_t *>(in.c_str()), in.size());
	send_data(reinterpret_cast<const uint8_t *>("\r\n"), 2);
}

comm *comm::deserialize(const JsonVariantConst j, bus *const b)
{
        std::string   type = j["comm-backend-type"];

        comm *d    = nullptr;

        if (type == "tcp-server")
                d = comm_tcp_socket_server::deserialize(j);
	else if (type == "tcp-client")
                d = comm_tcp_socket_client::deserialize(j);
#if defined(ESP32)
	else if (type == "hardware-serial")
                d = comm_esp32_hardwareserial::deserialize(j);
#endif
#if defined(ARDUINO)
	else if (type == "arduino")
                d = comm_arduino::deserialize(j);
#endif
#if IS_POSIX
	else if (type == "posix")
                d = comm_posix_tty::deserialize(j);
#endif
	else {
		DOLOG(warning, false, "comm::deserialize: \"%s\" not de-serialized", type.c_str());
		return nullptr;
	}

        assert(d);

        if (!d->begin()) {
		delete d;
		DOLOG(warning, false, "comm::deserialize: begin() \"%s\" failed", type.c_str());
		return nullptr;
	}

        return d;

}
