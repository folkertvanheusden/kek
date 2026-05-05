// reference: https://treasures.scss.tcd.ie/hardware/TCD-SCSS-T.20141120.008/EK-DELQA-UG-002.pdf
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


constexpr const uint8_t bc_addr[] { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };


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
}

bool deqna::begin()
{
	bool rc = false;
#if defined(linux)
	dev_fd = open_tun("pdp", mac_address);
	rc = dev_fd != -1;
#endif
	th_rx = new std::thread(&deqna::receiver, this);
	return rc;
}

deqna::~deqna()
{
	if (th_rx) {
		stop_flag = true;
		th_rx->join();
		delete th_rx;
	}
	if (dev_fd != -1)
		close(dev_fd);
}

void deqna::receiver()
{
	pollfd fds[] { { dev_fd, POLLIN, 0 } };

	uint32_t p_buffers = ((registers[3] & 63) << 16) | registers[2];

	while(!stop_flag) {
		// receive list invalid?
		if (registers[7] & 32) {
			myusleep(100000);
			continue;
		}

		int rc = poll(fds, 1, 100);
		if (rc == -1) {
			DOLOG(info, false, "deqna: tun device gone?");
			break;
		}
		if (rc == 0)
			continue;

		uint8_t buffer[1514];
		int byte_cnt = read(dev_fd, buffer, sizeof buffer);
		if (byte_cnt <= 0)
			break;

		// only for us or broadcast
		if (memcmp(buffer, mac_address, 6) != 0 && memcmp(buffer, bc_addr, 6) != 0)
			continue;

		DOLOG(debug, false, "deqna(rx): Ethernet packet received");

		// push into pdp memory
		bool     queued    = false;
		// a descriptor is 6 words
		while(p_buffers + 12 <= b->get_memory_size()) {
			auto     flags = b->read_unibus_word(p_buffers + 0 * 2);
			auto     ph    = b->read_unibus_word(p_buffers + 1 * 2);
			auto     pl    = b->read_unibus_word(p_buffers + 2 * 2);
			uint32_t chain = ((ph & 63) << 16) | pl;
			auto     len   = b->read_unibus_word(p_buffers + 3 * 2);  // buffer length, 2s complement
			int      length = ((~len & 0xffff) + 1) * 2;
			if ((ph & 0x8000) == 0) {  // valid?
				DOLOG(debug, false, "deqna(rx): %08o is an invalid RX descr", p_buffers);
				p_buffers = ((registers[3] & 63) << 16) | registers[2];
				break;
			}
			if ((ph & 0x4000) == 0) {  // chain? no, use as buffer
				DOLOG(debug, false, "deqna(rx): %08o is not a chain pointer, use as buffer-pointer", chain);
				for(int i=0; i<std::min(byte_cnt, length); i++)
					b->write_unibus_byte(chain + i, buffer[i]);

				uint16_t temp1 = b->read_unibus_word(p_buffers + 4 * 2);  // status word 1
				temp1 &= 0x3fff;  // upper 2 bits 0 is "This buffer contains the last segment of a message with no errors."
				b->write_unibus_word(p_buffers + 4 * 2, temp1);

				b->write_unibus_word(p_buffers + 4 * 2, (byte_cnt & 0x0700) | 0x00f8);  // FIXME odd byte count
				b->write_unibus_word(p_buffers + 5 * 2, ((byte_cnt & 0xff) << 8) | (byte_cnt & 0xff));  // mirrored
				b->write_unibus_word(p_buffers + 0 * 2, 0xffff);  // processed
				registers[7] |= 0x8000;  // RI
				if (registers[7] & 64) {  // IE
					uint16_t vector = registers[6] & 0x3fc;
					DOLOG(debug, false, "deqna(rx): packet queued, trigger %06o", vector);
					b->getCpu()->queue_interrupt(5, vector);
					queued = true;
				}
				break;
			}
			if (ph & 0x4000)
				p_buffers = chain;
			else
				p_buffers += 12;
		}

		if (!queued)
			DOLOG(debug, false, "deqna(rx): packet NOT queued");
	}

	DOLOG(info, false, "deqna RECEIVER THREAD TERMINATING");
}

void deqna::transmitter()
{
	// sender list invalid?
	if (registers[7] & 16)
		return;

	uint8_t buffer[1514];
	int     buffer_offset = 0;

	// get packet from pdp memory
	uint32_t p_buffers = ((registers[5] & 63) << 16) | registers[4];
	bool     queued    = false;
	// a descriptor is 6 words
	while(p_buffers + 12 <= b->get_memory_size()) {
		auto     flags = b->read_unibus_word(p_buffers + 0 * 2);
		auto     ph    = b->read_unibus_word(p_buffers + 1 * 2);
		auto     pl    = b->read_unibus_word(p_buffers + 2 * 2);
		uint32_t chain = ((ph & 63) << 16) | pl;
		auto     len   = b->read_unibus_word(p_buffers + 3 * 2);  // buffer length, 2s complement
		int      length = ((~len & 0xffff) + 1) * 2;

		DOLOG(debug, false, "deqna(tx): checking descr at %08o, points to %08o which is %d bytes (0x%04x | %04x)", p_buffers, chain, length, len, ~len);

		if ((ph & 0x8000) == 0) {  // valid?
			DOLOG(debug, false, "deqna(tx): %08o is end of BDL", p_buffers);
			break;
		}
		else {
			b->write_unibus_word(p_buffers + 0, 0xffff);  // buffer busy

			if ((ph & 0x4000) == 0x0000) {  // chain? no, use as buffer
				DOLOG(debug, false, "deqna(tx): %08o is not a chain pointer, use as buffer-pointer", chain);
				if (length > 2048) {
					DOLOG(debug, false, "deqna(tx): buffer has invalid size %d", length);
					break;
				}
				if (chain + length > b->get_memory_size()) {
					DOLOG(debug, false, "deqna(tx): buffer does not fit in RAM");
					break;
				}

				DOLOG(info, false, "flags: %06o, ph: %06o, status1: %06o, status2: %06o", flags, ph, b->read_unibus_word(p_buffers + 4 * 2), b->read_unibus_word(p_buffers + 5 * 2));

				for(int i=0; i<length && buffer_offset < sizeof(buffer); i++)
					buffer[buffer_offset++] = b->read_unibus_byte(chain + i);
			}

			flags &= ~0x4000;  // buffer no longer busy
			b->write_unibus_word(p_buffers + 0, flags);
		}

		if (ph & 0x2000) {  // END bit
			if (buffer_offset == 0) {
				DOLOG(warning, false, "deqna(tx): failed transmitting - empty buffer");
			}
			else {
				int tx_rc = write(dev_fd, buffer, buffer_offset);
				if (tx_rc <= 0) {
					DOLOG(warning, false, "deqna(tx): failed transmitting - device down?");
					break;
				}

				buffer_offset = 0;
			}

			b->write_unibus_word(p_buffers + 4 * 2, 0x2000);  // all good
			b->write_unibus_word(p_buffers + 5 * 2, 0);  // TDR

			uint16_t temp = registers[7];
			DOLOG(debug, false, "deqna(tx): register 7=0x%04x", temp);
			registers[7] |= 128;  // XI
			if (registers[7] & 64) {  // IE
				uint16_t vector = registers[6] & 0x3fc;
				DOLOG(debug, false, "deqna(tx): packet sent, trigger %06o", vector);
				b->getCpu()->queue_interrupt(5, vector);
				queued = true;
			}
		}

		if ((ph & 0x8000) == 0)
			break;

		if (ph & 0x4000)
			p_buffers = chain;
		else
			p_buffers += 12;
	}

	if (!queued)
		DOLOG(debug, false, "deqna(tx): packet NOT queued");
}

void deqna::reset()
{
	DOLOG(debug, false, "deqna reset");

	for(int i=0; i<8; i++)
		registers[i] = 0;
	registers[6] = 0774;
	registers[7] = // 0x100 |  // IL is asserted initially, low active
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
//		rc |= 0x2000;  // carrier detected
		rc |= 0x1000;  // fuse ok
	}

	DOLOG(debug, false, "deqna read from %06o (%d): %06o", addr, reg_nr, rc);

	return rc;
}

void deqna::write_byte(const uint16_t addr, const uint8_t v)
{
	// just for completeness: the deqna only supports word-access
	int reg_nr = (addr - DEQNA_BASE) / 2;
	DOLOG(debug, false, "deqna write_b %03o to %06o (%d)", v, addr, reg_nr);
        uint16_t vtemp = registers[reg_nr];
        update_word(&vtemp, addr & 1, v);
        write_word(addr, vtemp);
}

void deqna::write_word(const uint16_t addr, const uint16_t v)
{
	int reg_nr = (addr - DEQNA_BASE) / 2;
	DOLOG(debug, false, "deqna write %06o to %06o (%d)", v, addr, reg_nr);

	uint16_t old_v = registers[reg_nr];
	registers[reg_nr] = v;

	if (addr == DEQNA_CSR) {
		uint16_t new_csr = old_v & 0x7834;  // clear RI/XI and bits settable by software
		new_csr |= (v & 0x074b);  // only allow certain bits
		new_csr &= ~(v & 0x8080);         // if SW writes 1 to RI or XI, clear them
		registers[7] = new_csr;
	}
	else if (addr == DEQNA_RX_BDLH) {
		registers[7] &= ~32;  // RX buffers set, no more invalid
	}
	else if (addr == DEQNA_TX_BDLH) {
		registers[7] &= ~16;  // TX buffers set, no more invalid
		transmitter();
	}
	else if (addr == DEQNA_VECTOR) {
		registers[reg_nr] &= 0x7fd;  // mask off unused bits but keep QE_VEC_ID enabled
	}
}
