// (C) 2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#if WITH_PPS
#include <fcntl.h>
#include <optional>
#include <sys/timepps.h>

#include "comm_pst.h"
#include "utils.h"


comm_pst::comm_pst(const std::string & dev_name) : dev_name(dev_name)
{
}

comm_pst::~comm_pst()
{
	if (th) {
		stop_flag = true;
		th->join();
		delete th;
	}
}

bool comm_pst::begin()
{
	th = new std::thread(std::ref(*this));
	return true;
}

std::string comm_pst::get_identifier() const
{
	return "PST/Traconex 1020";
}

bool comm_pst::is_connected()
{
	return true;
}

std::optional<std::pair<pps_handle_t, int> > open_pps(const char *const filename)
{
        int fd = open(filename, O_RDWR);
        if (fd == -1) {
		DOLOG(log_ss::LS_COMM, "Cannot open %s: %s", filename, strerror(errno));
                return { };
        }

        pps_handle_t handle { };
        time_pps_create(fd, &handle);

        int available_modes { 0 };
        time_pps_getcap(handle, &available_modes);
        if ((available_modes & PPS_CAPTUREASSERT) == 0) {
                DOLOG(log_ss::LS_COMM, "Cannot CAPTUREASSERT from %s: %s", filename, strerror(errno));
                close(fd);
		return { };
        }

        pps_params_t params { };
        time_pps_getparams(handle, &params);
        params.mode |= PPS_CAPTUREASSERT;
        time_pps_setparams(handle, &params);

        return { { handle, fd } };
}

void comm_pst::operator()()
{
	auto handles = open_pps(dev_name.c_str());
	if (handles.has_value() == false)
		return;

	while(!stop_flag) {
                pps_info_t infobuf { };
                if (time_pps_fetch(handles.value().first, PPS_TSFMT_TSPEC, &infobuf, nullptr) == -1) {
                        DOLOG(log_ss::LS_COMM, "Cannot time_pps_fetch from %s: %s", dev_name, strerror(errno));
                        break;
                }

		DOLOG(log_ss::LS_COMM, "PPS %ld.%09ld", infobuf.assert_timestamp.tv_sec, infobuf.assert_timestamp.tv_nsec);

		tm *tm = gmtime(&infobuf.assert_timestamp.tv_sec);

		uint8_t new_msg_buffer[32];
		memset(new_msg_buffer, '0', sizeof new_msg_buffer);
		new_msg_buffer[ 0] = '4';
		new_msg_buffer[ 1] = '0';
		int ms = infobuf.assert_timestamp.tv_nsec / 1'000'000;
		new_msg_buffer[ 2] = '0' + ms / 64;
		new_msg_buffer[ 3] = '0' + (ms & 63);
		new_msg_buffer[ 4] = '0' + tm->tm_sec;
		new_msg_buffer[ 5] = '0' + tm->tm_min;
		new_msg_buffer[ 6] = '0' + tm->tm_hour;
		new_msg_buffer[ 7] = '0' + tm->tm_yday / 64;
		new_msg_buffer[ 8] = '0' + (tm->tm_yday & 63);
		new_msg_buffer[ 9] = '0' + (tm->tm_year + 1900 - 1986);
		new_msg_buffer[31] = '\n';

		my_unique_lock lck(&msg_buffer_lock);
		memcpy(msg_buffer, new_msg_buffer, 32);
	}

	close(handles.value().second);
}

bool comm_pst::has_data()
{
	my_unique_lock lck(&msg_buffer_lock);
	return mb_offset < 32;
}

uint8_t comm_pst::get_byte()
{
	my_unique_lock lck(&msg_buffer_lock);
	if (mb_offset >= 32)
		return 0;

	return msg_buffer[mb_offset++];
}

void comm_pst::send_data(const uint8_t *const, const size_t)
{
	// ignore
}

#if IS_POSIX
JsonDocument comm_pst::serialize() const
{
	JsonDocument j;
	j["dev-name"] = dev_name;
	// msg_buffer & mb_offset FIXME
	return j;
}

comm_pst *comm_pst::deserialize(const JsonVariantConst j)
{
	comm_pst *r = new comm_pst(j["dev-name"].as<std::string>());
	r->begin();
	return r;
}
#endif
#endif
