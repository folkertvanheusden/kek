#include "gen.h"
#include <esp_eth.h>
#include <esp_vfs_l2tap.h>

#include "eth_transport_esp32.h"
#include "log.h"
#include "utils.h"


// somewhat based on https://github.com/espressif/esp-idf/blob/v6.0.1/examples/protocols/l2tap/main/l2tap_main.c

#define ETH_INTERFACE "ETH_DEF"

static void init_l2tap_fd(int flags, int *const fd, uint8_t *const mac_addr)
{
	*fd = open("/dev/net/tap", flags);
	if (*fd == -1) {
		DOLOG(ll_error, false, "Unable to open L2 TAP interface, errno: %d", errno);
		return;
	}

	// Configure Ethernet interface on which to get raw frames
	if (int ret = ioctl(*fd, L2TAP_S_INTF_DEVICE, ETH_INTERFACE); ret == -1) {
		DOLOG(ll_error, false, "Unable to bind L2 TAP fd %d with Ethernet device, errno: %d", fd, errno);
		close(*fd);
		*fd = -1;
		return;
	}

	esp_eth_handle_t eth_hndl { };
	if (ioctl(*fd, L2TAP_G_DEVICE_DRV_HNDL, &eth_hndl) == -1) {
		DOLOG(ll_error, false, "failed to get socket eth_handle: %d", errno);
		close(*fd);
		*fd = -1;
		return;
	}

	esp_eth_ioctl(eth_hndl, ETH_CMD_G_MAC_ADDR, mac_addr);
	DOLOG(debug, false, "fd %d Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x", fd,
			mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
}

constexpr const int max_pkt_size = 1512;

eth_transport_esp32::eth_transport_esp32()
{
}

eth_transport_esp32::~eth_transport_esp32()
{
	if (fd != -1)
		close(fd);
}

bool eth_transport_esp32::begin()
{
	init_l2tap_fd(0, &fd, mac_addr);
	return fd != -1;
}

std::string eth_transport_esp32::identifier() const
{
	return "esp32";
}

void eth_transport_esp32::transmit(const uint8_t *const data, const size_t n_bytes)
{
	write(fd, data, n_bytes);
}

std::pair<uint8_t *, size_t> eth_transport_esp32::get(const int timeout)
{
	timeval tv { };
        tv.tv_sec  = timeout / 1000000;
        tv.tv_usec = timeout % 1000000;

        fd_set rfds { };
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        int ret_sel = select(fd + 1, &rfds, nullptr, nullptr, &tv);
	if (ret_sel > 0 && FD_ISSET(fd, &rfds)) {
		uint8_t buffer[max_pkt_size];
		if (int rc = read(fd, buffer, sizeof buffer); rc > 14) {
			uint8_t *out = new uint8_t[rc];
			memcpy(out, buffer, rc);
			return { out, rc };
		}
	}

	return { nullptr, 0 };
}
