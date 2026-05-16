// (C) 2026-2025 by Folkert van Heusden
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
#include "comm_esp32_SC16IS752.h"
#endif
#if IS_POSIX
#include "comm_posix_tty.h"
#endif
#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
#include "comm_tcp_socket_client.h"
#include "comm_tcp_socket_server.h"
#endif
#include "log.h"


#if defined(ESP32)
SC16IS752 *comm::ser2_inst_1 { nullptr };
SC16IS752 *comm::ser2_inst_2 { nullptr };
#endif

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

#if defined(ESP32)
void comm::set_comm(SC16IS752 *const a, SC16IS752 *const b)
{
	ser2_inst_1 = a;
	ser2_inst_2 = b;
}
#endif

comm *comm::deserialize(const JsonVariantConst j, bus *const b)
{
        std::string   type = j["comm-backend-type"];

        comm *d    = nullptr;

	if (false) {
	}
#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
	else if (type == "tcp-server")
                d = comm_tcp_socket_server::deserialize(j);
	else if (type == "tcp-client")
                d = comm_tcp_socket_client::deserialize(j);
#endif
#if defined(ESP32)
	else if (type == "SC16IS752-serial")
                d = comm_esp32_SC16IS752::deserialize(j, ser2_inst_1, ser2_inst_2);
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
