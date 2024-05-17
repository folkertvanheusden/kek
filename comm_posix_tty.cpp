// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#if defined(_WIN32)
#include <ws2tcpip.h>
#include <winsock2.h>
#else
#include <poll.h>
#endif

#include "comm_posix_tty.h"
#include "log.h"


comm_posix_tty::comm_posix_tty(const std::string & device, const int bitrate)
{
	fd = open(device.c_str(), O_RDWR);
	if (fd == -1) {
		DOLOG(warning, false, "com_posix_tty failed to access %s: %s", device.c_str(), strerror(errno));
		return;  // TODO error handling
	}

	// from https://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c
	termios tty { };
        if (tcgetattr(fd, &tty) == -1) {
		DOLOG(warning, false, "com_posix_tty tcgetattr failed: %s", strerror(errno));
		close(fd);
                return;
	}

        cfsetospeed(&tty, bitrate);
        cfsetispeed(&tty, bitrate);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
        // disable IGNBRK for mismatched speed tests; otherwise receive break
        // as \000 chars
        tty.c_iflag &= ~IGNBRK;         // disable break processing
        tty.c_lflag = 0;                // no signaling chars, no echo,
                                        // no canonical processing
        tty.c_oflag = 0;                // no remapping, no delays
        tty.c_cc[VMIN]  = 0;            // read doesn't block
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

        tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
        tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        if (tcsetattr(fd, TCSANOW, &tty) == -1) {
		DOLOG(warning, false, "com_posix_tty tcsetattr failed: %s", strerror(errno));
		close(fd);
		return;
	}
}

comm_posix_tty::~comm_posix_tty()
{
	close(fd);
}

bool comm_posix_tty::is_connected()
{
	return true;
}

bool comm_posix_tty::has_data()
{
#if defined(_WIN32)
	WSAPOLLFD fds[] { { fd, POLLIN, 0 } };
	int rc = WSAPoll(fds, 1, 0);
#else
	pollfd    fds[] { { fd, POLLIN, 0 } };
	int rc = poll(fds, 1, 0);
#endif

	return rc == 1;
}

uint8_t comm_posix_tty::get_byte()
{
	uint8_t c = 0;
	read(fd, &c, 1);  // TODO error handling

	return c;
}

void comm_posix_tty::send_data(const uint8_t *const in, const size_t n)
{
	const uint8_t *p   = in;
	size_t         len = n;

	while(len > 0) {
		int rc = write(fd, p, len);
		if (rc <= 0)  // TODO error checking
			break;

		p   += rc;
		len -= rc;
	}
}