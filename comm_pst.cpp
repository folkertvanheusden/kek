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

		DOLOG(log_ss::LS_COMM, "PPS");

		tm *tm = gmtime(&infobuf.assert_timestamp.tv_sec);
		std::string new_msg_buffer =
			format(" %02d:%02d:%02d.%03d\r", tm->tm_hour, tm->tm_min, tm->tm_sec, infobuf.assert_timestamp.tv_nsec / 1'000'000) +
			format("%02d/%02d/%02d/%03d\r", (tm->tm_year + 1900) % 100, tm->tm_mon + 1, tm->tm_mday, tm->tm_yday) +
			"O6@095281804C00000394\r";

		my_unique_lock lck(&msg_buffer_lock);
		msg_buffer = new_msg_buffer;
	}

	close(handles.value().second);
}

bool comm_pst::has_data()
{
	my_unique_lock lck(&msg_buffer_lock);
	return msg_buffer.empty() == false;
}

uint8_t comm_pst::get_byte()
{
	my_unique_lock lck(&msg_buffer_lock);
	if (msg_buffer.empty())
		return 0;

	char c = msg_buffer[0];
	msg_buffer = msg_buffer.substr(1);
	return c;
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
