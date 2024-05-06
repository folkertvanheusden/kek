// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include <Arduino.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "FvHNTP.h"


#define NTP_EPOCH uint64_t(86400ll * (365ll * 70ll + 17ll))

struct sntp_datagram
{
        uint8_t  mode : 3;
        uint8_t  vn   : 3;
        uint8_t  li   : 2;
        uint8_t  stratum;
        int8_t   poll;
        int8_t   precision;
        uint32_t root_delay;
        uint32_t root_dispersion;
        uint32_t reference_identifier;
        uint32_t reference_timestamp_secs;
        uint32_t reference_timestamp_fraq;
        uint32_t originate_timestamp_secs;
        uint32_t originate_timestamp_fraq;
        uint32_t receive_timestamp_seqs;
        uint32_t receive_timestamp_fraq;
        uint32_t transmit_timestamp_secs;
        uint32_t transmit_timestamp_fraq;
};

ntp::ntp(const std::string & server): server(server)
{
}

ntp::~ntp()
{
	stop = true;

	if (th) {
		th->join();
		delete th;
	}
}

void ntp::begin()
{
	th = new std::thread(std::ref(*this));
}

std::optional<uint64_t> ntp::get_unix_epoch_ms()
{
	std::unique_lock<std::mutex> lck(lock);

	if (ntp_at_ts == 0)
		return { };

	auto now = millis();

	return ntp_at_ts + now - millis_at_ts - NTP_EPOCH * 1000;
}

uint64_t get_ms_from_ntp(const uint32_t high, const uint32_t low)
{
	return uint64_t(ntohl(high)) * 1000ll + ntohl(low) / (4295 * 1000);
}

void ntp::operator()()
{
	int           fd            = socket(PF_INET, SOCK_DGRAM, 0);
	sockaddr_in   server_addr { };
	server_addr.sin_family      = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(server.c_str());
	server_addr.sin_port        = htons(123);

	sntp_datagram packet_out;

	while(!stop) {
		int s = 5;

		memset(&packet_out, 0x00, sizeof(packet_out));
		packet_out.vn      = 4;
		packet_out.mode    = 3;
		packet_out.stratum = 14;
		packet_out.poll    = 2;

		auto now = get_unix_epoch_ms();

		if (now.has_value()) {
			uint64_t sec  = now.value() / 1000;
			uint64_t usec = (now.value() % 1000) * 1000;

			packet_out.originate_timestamp_secs = htonl(sec + NTP_EPOCH);  // T1
			packet_out.originate_timestamp_fraq = htonl(usec * 4295);
		}

		if (sendto(fd, &packet_out, sizeof(packet_out), 0, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == sizeof(packet_out)) {
			sntp_datagram packet_in { 0 };

			// TODO verify source address
			if (recvfrom(fd, &packet_in, sizeof(packet_in), 0, nullptr, nullptr) == sizeof(packet_in)) {
				// TODO verify version etc
				uint32_t now  = millis();
				auto     t_t4 = get_unix_epoch_ms();

				std::unique_lock<std::mutex> lck(lock);
				millis_at_ts = now;

				s = 60;

				if (t_t4.has_value()) {
					int64_t t1     = get_ms_from_ntp(packet_out.originate_timestamp_secs, packet_out.originate_timestamp_fraq);
					int64_t t2     = get_ms_from_ntp(packet_in.receive_timestamp_seqs,    packet_in.receive_timestamp_fraq   );
					int64_t t3     = get_ms_from_ntp(packet_in.transmit_timestamp_secs,   packet_in.transmit_timestamp_fraq  );
					int64_t t4     = t_t4.value() + NTP_EPOCH * 1000;

					auto    offset = ((t2 - t1) + (t3 - t4)) / (2 * 1000);

					if (millis_at_ts > offset)
						millis_at_ts -= offset;
					else {
						millis_at_ts = 0;
						s            = 4;
					}
				}
				else {
					ntp_at_ts = get_ms_from_ntp(packet_in.transmit_timestamp_secs, packet_in.transmit_timestamp_fraq);
				}
			}
		}

		sleep(s);
	}

	close(fd);
}
