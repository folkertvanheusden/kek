#include "gen.h"
#if IS_POSIX
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "eth_transport_linux.h"
#include "log.h"
#include "utils.h"


static void set_ifr_name(ifreq *ifr, const std::string & dev_name)
{
	memset(ifr->ifr_name, 0x00, IFNAMSIZ);
	size_t copy_name_n = std::min(size_t(IFNAMSIZ), dev_name.size());
	memcpy(ifr->ifr_name, dev_name.c_str(), copy_name_n);
}

static bool invoke_if_ioctl(const std::string & dev_name, const int ioctl_nr, ifreq *const p)
{
	int temp_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (temp_fd == -1)
		DOLOG(ll_error, false, "create socket failed");
	else {
		set_ifr_name(p, dev_name);

		bool ok = true;
		if (ioctl(temp_fd, ioctl_nr, p) == -1) {
			DOLOG(ll_error, false, "deqna: ioctl %d failed: %s", ioctl_nr, strerror(errno));
			ok = false;
		}

		close(temp_fd);

		return ok;
	}

	return false;
}

static int open_tun(const std::string & dev_name)
{
	int fd      = -1;
	int temp_fd = -1;

	do {
		fd = open("/dev/net/tun", O_RDWR);
		if (fd == -1) {
			DOLOG(ll_error, false, "cannot open /dev/net/tun");
			break;
		}

		if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
			DOLOG(ll_error, false, "FD_CLOEXEC on fd failed");
			break;
		}

		ifreq ifr_tap { };
		ifr_tap.ifr_flags = IFF_TAP | IFF_NO_PI;
		set_ifr_name(&ifr_tap, dev_name);

		if (ioctl(fd, TUNSETIFF, &ifr_tap) == -1) {
			DOLOG(ll_error, false, "ioctl TUNSETIFF failed");
			break;
		}

		//
		ifr_tap.ifr_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
		if (invoke_if_ioctl(dev_name, SIOCSIFFLAGS, &ifr_tap) == false)
			break;

		close(temp_fd);

		return fd;
	}
	while(0);

	if (temp_fd != -1)
		close(temp_fd);

	if (fd != -1)
		close(fd);

	return -1;
}

eth_transport_linux::eth_transport_linux(const std::string & dev_name) :
	dev_name(dev_name)
{
}

eth_transport_linux::~eth_transport_linux()
{
	if (fd != -1)
		close(fd);
}

bool eth_transport_linux::begin()
{
	fd = open_tun(dev_name);
	return fd != -1;
}

std::string eth_transport_linux::identifier() const
{
	return "linux:" + dev_name;
}

void eth_transport_linux::transmit(const uint8_t *const data, const size_t n_bytes)
{
	WRITE(fd, reinterpret_cast<const char *>(data), n_bytes);
}

std::pair<uint8_t *, size_t> eth_transport_linux::get(const int timeout)
{
	pollfd fds[] { { fd, POLLIN, 0 } };
	int    rc = poll(fds, 1, timeout);
	if (rc <= 0)
		return { nullptr, 0 };

	constexpr const int max_pkt_size = 1512;
	uint8_t *pkt = new uint8_t[max_pkt_size]();
	int      rc2 = read(fd, pkt, max_pkt_size);
	if (rc2 == -1) {
		delete [] pkt;
		pkt = nullptr;
		rc2 = 0;
	}

	return { pkt, rc2 };
}
#endif
