#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "deqna.h"
#include "log.h"


#if defined(linux)
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

static int open_tun(const std::string & dev_name, const uint8_t mac_address[6])
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

		if (invoke_if_ioctl(dev_name, SIOCGIFHWADDR, &ifr_tap) == false)
			break;
		if (ifr_tap.ifr_hwaddr.sa_family != ARPHRD_ETHER) {
			DOLOG(ll_error, false, "unexpected adress family %d", ifr_tap.ifr_hwaddr.sa_family);
			break;
		}
		memcpy(ifr_tap.ifr_hwaddr.sa_data, mac_address, 6);
		if (invoke_if_ioctl(dev_name, SIOCSIFHWADDR, &ifr_tap) == false)
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
#endif

deqna::deqna(bus *const b, const uint8_t mac_address[6]) :
	b(b)
{
	memcpy(this->mac_address, mac_address, sizeof this->mac_address);
	reset();
#if defined(linux)
	dev_fd = open_tun("pdp", mac_address);
#endif
	th_rx = new std::thread(&deqna::receiver,    this);
	th_tx = new std::thread(&deqna::transmitter, this);
}

deqna::~deqna()
{
	if (dev_fd != -1)
		close(dev_fd);
	stop_flag = true;
	if (th_tx) {
		th_tx->join();
		delete th_tx;
	}
	if (th_rx) {
		th_rx->join();
		delete th_rx;
	}
}

void deqna::receiver()
{
	pollfd fds[] { { dev_fd, POLLIN, 0 } };

	while(!stop_flag) {
		// receive list invalid?
		if (registers[7] & 32) {
			myusleep(100000);
			continue;
		}
		///////////////////
		uint32_t p_buffers = ((registers[3] & 63) << 22) | registers[2];
		// a descriptor is 6 words
		while(p_buffers + 12 < b->get_memory_size()) {
			auto     ph    = b->peek_word(0, p_buffers + 1 * 2);
			auto     pl    = b->peek_word(0, p_buffers + 2 * 2);
			if (ph.has_value() == false || pl.has_value() == false)
				break;
			uint32_t chain = ((ph.value() >> 10) << 16) | pl.value();
			if (chain == 0 || (pl.value() & 1) == 0)
				break;
			auto     len   = b->peek_word(0, p_buffers + 3 * 2);  // buffer length in 3d word
			if (len.has_value() == false)
				break;
			uint16_t length = -int16_t(((len.value() & 0xff) << 8) | (len.value() >> 8));
			printf("RX %08x %d\n", p_buffers, length);
			p_buffers += 12;
		}
		///////////////////

		int rc = poll(fds, 1, 100);
		if (rc == -1) {
			DOLOG(info, false, "deqna: tun device gone?");
			break;
		}
		if (rc == 0)
			continue;

		uint8_t buffer[1512];
		int byte_cnt = read(dev_fd, buffer, sizeof buffer);
		if (byte_cnt <= 0)
			break;

		// only for us or broadcast
		if (memcmp(buffer, mac_address, 6) != 0 && memcmp(buffer, bc_addr, 6) != 0)
			continue;

		// TODO push into pdp memory
	}
}

void deqna::transmitter()
{
	while(!stop_flag) {
		// 67.2 uS for the shortest packet including IFG (inter-
		// frame gap)
		myusleep(250);  // rounded up slightly

		// sender list invalid?
		if (registers[7] & 16)
			continue;

		uint32_t p_buffers = ((registers[5] & 63) << 22) | registers[4];
		// a descriptor is 6 words
		while(p_buffers + 12 < b->get_memory_size()) {
			auto     ph    = b->peek_word(0, p_buffers + 1 * 2);
			auto     pl    = b->peek_word(0, p_buffers + 2 * 2);
			if (ph.has_value() == false || pl.has_value() == false)
				break;
			uint32_t chain = ((ph.value() >> 10) << 16) | pl.value();
			if (chain == 0 || (pl.value() & 1) == 0)
				break;
			auto     len   = b->peek_word(0, p_buffers + 3 * 2);  // buffer length in 3d word
			if (len.has_value() == false)
				break;
			uint16_t length = -int16_t(((len.value() & 0xff) << 8) | (len.value() >> 8));
			printf("TX %08x %d\n", p_buffers, length);
			p_buffers += 12;
		}
	}
}

void deqna::reset()
{
	DOLOG(info, false, "deqna reset");

	memset(registers, 0x00, sizeof registers);
	registers[6] = 0774;
	registers[7] = 0x100 |  // IL is on initially
		32 |  // receive list invalid
		16;  // transmit list invalid
}

void deqna::show_state(console *const cnsl) const
{
}

uint16_t deqna::read_word(const uint16_t addr)
{
	int      reg_nr = (addr - DEQNA_BASE) / 2;
	uint16_t rc     = registers[reg_nr];

	if (reg_nr < 6)  // MAC address in low byte from first 6 words
		rc = mac_address[reg_nr];

	if (reg_nr == 7) {  // CSR
		rc |= 0x2000;  // carrier detected
		rc |= 0x1000;  // fuse ok
	}

	DOLOG(info, false, "deqna read from %06o (%d): %06o", addr, reg_nr, rc);

	return rc;
}

void deqna::write_byte(const uint16_t addr, const uint8_t v)
{
	int reg_nr = (addr - DEQNA_BASE) / 2;
	DOLOG(info, false, "deqna write %03o to %06o (%d)", v, addr, reg_nr);
}

void deqna::write_word(const uint16_t addr, uint16_t v)
{
	int reg_nr = (addr - DEQNA_BASE) / 2;
	DOLOG(info, false, "deqna write %06o to %06o (%d)", v, addr, reg_nr);

	registers[reg_nr] = v;

	if (addr == DEQNA_CSR) {
		registers[7] &= 0x7fff;  // clear RI (receive interrupt request)
	}
	else if (addr == DEQNA_RX_BDLH) {
		registers[7] &= ~32;  // RX buffers set, no more invalid
	}
	else if (addr == DEQNA_TX_BDLH) {
		registers[7] &= ~16;  // TX buffers set, no more invalid
	}
	else if (addr == DEQNA_VECTOR) {
		registers[reg_nr] &= 0x7fd;  // mask off unused bits but keep QE_VEC_ID enabled
	}
}
