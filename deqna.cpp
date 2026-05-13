// reference: https://treasures.scss.tcd.ie/hardware/TCD-SCSS-T.20141120.008/EK-DELQA-UG-002.pdf
#include "gen.h"
#include <cstring>
#include <fcntl.h>
#if !defined(BUILD_FOR_RP2040)
#if defined(IS_POSIX)
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif
#endif
#include <unistd.h>
#if defined(linux)
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>
#endif

#include "deqna.h"
#include "log.h"


constexpr const uint8_t bc_addr[] { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

#if defined(BUILD_FOR_RP2040)
static void thread_wrapper_receiver_low(void *p)
{
       deqna *const deqna_ = reinterpret_cast<deqna *>(p);

       deqna_->receiver_low();
}

static void thread_wrapper_receiver_high(void *p)
{
       deqna *const deqna_ = reinterpret_cast<deqna *>(p);

       deqna_->receiver_high();
}
#endif

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
#endif

deqna::deqna(bus *const b, const uint8_t mac_address[6]) :
	b(b)
{
	memcpy(this->mac_address, mac_address, sizeof this->mac_address);
	reset(true);
}

bool deqna::begin()
{
	bool rc = false;
#if defined(linux)
	dev_fd = open_tun("pdp");
	rc = dev_fd != -1;
#endif
#if defined(BUILD_FOR_RP2040)
	xTaskCreate(&thread_wrapper_receiver_low,  "deqna-rl", 3072, this, 1, nullptr);
	xTaskCreate(&thread_wrapper_receiver_high, "deqna-rh", 3072, this, 1, nullptr);
#else
	th_rx_low  = new std::thread(&deqna::receiver_low,  this);
	th_rx_high = new std::thread(&deqna::receiver_high, this);
#endif
	return rc;
}

deqna::~deqna()
{
	stop_flag = true;
	if (th_rx_low) {
		th_rx_low->join();
		delete th_rx_low;
	}
	if (th_rx_high) {
		th_rx_high->join();
		delete th_rx_high;
	}
	if (dev_fd != -1)
		close(dev_fd);
	purge_buffers();
}

void deqna::purge_buffers()
{
	while(received.is_empty() == false) {
		auto item = received.pop(1000);
		if (item.has_value())
			delete [] item.value().first;
	}
}

void deqna::queue_rx_packet(const uint8_t *const in, const size_t n)
{
	if (received.aprox_size() < DEQNA_MAX_N_QUEUED) {
		uint8_t *copy = new uint8_t[n];
		memcpy(copy, in, n);
		received.push({ copy, n });
	}
	else {
		DOLOG(debug, false, "deqna: rx queue full, packet dropped");
	}
}

// receiver pushes packets on a queue and signals another thread
// to process it. this allows loopback
void deqna::receiver_low()
{
	set_thread_name("deqna:rx_low");

#if defined(IS_POSIX)
	pollfd fds[] { { dev_fd, POLLIN, 0 } };

	while(!stop_flag) {
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

		if (byte_cnt < 14)
			continue;

		// only for us or broadcast
		if (memcmp(buffer, mac_address, 6) != 0 && memcmp(buffer, bc_addr, 6) != 0)
			continue;

		if (registers[7] & 1) {  // receiver enabled?
			DOLOG(debug, false, "deqna packet received from real Ethernet");
			queue_rx_packet(buffer, byte_cnt);
		}
		else {
			DOLOG(debug, false, "deqna dropped packet: receiver not enabled");
		}
	}
#endif

	DOLOG(info, false, "deqna LOW RECEIVER THREAD TERMINATING");
}

void deqna::receiver_high()
{
	set_thread_name("deqna:rx_high");

	while(!stop_flag) {
		// receive list invalid?
		if (registers[7] & 32) {
			myusleep(100000);
			continue;
		}

		std::optional<std::pair<uint8_t *, size_t> > item;
		while(!stop_flag) {
			item = received.pop(100);
			if (item.has_value())
				break;
		}
		if (stop_flag)
			break;
		
		const uint8_t *const buffer   = item.value().first;
		const size_t         byte_cnt = item.value().second;

		DOLOG(debug, false, "deqna(rx): Ethernet packet received (%zu bytes, from %02x:%02x:%02x:%02x:%02x:%02x, type: %04x)",
				byte_cnt,
				buffer[6], buffer[7], buffer[8], buffer[9], buffer[10], buffer[11], (buffer[12] << 8) | buffer[13]);

		uint32_t p_buffers = ((registers[3] & 63) << 16) | registers[2];
		DOLOG(debug, false, "deqna(rx): RBL is at %08o", p_buffers);

		// push into pdp memory
		bool     queued    = false;
		// a descriptor is 6 words
		while(p_buffers + 12 <= b->get_memory_size()) {
			auto     ph    = b->read_unibus_word(p_buffers + 1 * 2);
			auto     pl    = b->read_unibus_word(p_buffers + 2 * 2);
			uint32_t chain = ((ph & 63) << 16) | pl;
			auto     len   = b->read_unibus_word(p_buffers + 3 * 2);  // buffer length, 2s complement
			int      length = ((~len & 0xffff) + 1) * 2;
			if ((ph & 0x8000) == 0) {  // valid?
				DOLOG(debug, false, "deqna(rx): %08o is an end maker", p_buffers);
				break;
			}
			if ((ph & 0x4000) == 0) {  // chain? no, use as buffer
				DOLOG(info, false, "deqna(rx): flags: %06o, ph: %06o, status1: %06o, status2: %06o", b->read_unibus_word(p_buffers + 0 * 2), ph, b->read_unibus_word(p_buffers + 4 * 2), b->read_unibus_word(p_buffers + 5 * 2));
				DOLOG(debug, false, "deqna(rx): %08o is not a chain pointer, use as buffer-pointer (%d bytes)", chain, length);
				b->write_unibus_word(p_buffers + 0 * 2, 0xffff);  // processing
				for(size_t i=0; i<std::min(byte_cnt, size_t(length)); i++)
					b->write_unibus_byte(chain + i, buffer[i]);

				size_t temp = std::max(byte_cnt, size_t(60)) - 60;  // frames are padded
				b->write_unibus_word(p_buffers + 4 * 2, (temp & 0x0700) | 0x00f8);  // FIXME odd byte count
				b->write_unibus_word(p_buffers + 5 * 2, ((temp & 0xff) << 8) | (temp & 0xff));  // mirrored
				b->write_unibus_word(p_buffers + 0 * 2, 0x0200 | 0x0100);  // processed
				b->write_unibus_word(p_buffers + 1 * 2, 0);
				registers[7] |= 0x8000;  // RI
				if (registers[7] & 64) {  // IE
					uint16_t vector = registers[6] & 0x3fc;
					DOLOG(debug, false, "deqna(rx): packet queued, trigger %06o", vector);
					b->getCpu()->queue_interrupt(DEQNA_IRQ_LEVEL, vector);
					queued = true;
				}
				break;
			}
			if (ph & 0x4000)
				p_buffers = chain;
			else
				p_buffers += 12;
		}

		delete [] buffer;

		registers[7] |= 32;

		if (!queued)
			DOLOG(debug, false, "deqna(rx): packet NOT queued");
	}

	DOLOG(info, false, "deqna HIGH RECEIVER THREAD TERMINATING");
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

				DOLOG(info, false, "deqna(tx): flags: %06o, ph: %06o, status1: %06o, status2: %06o", flags, ph, b->read_unibus_word(p_buffers + 4 * 2), b->read_unibus_word(p_buffers + 5 * 2));

				for(int i=0; i<length && size_t(buffer_offset) < sizeof(buffer); i++)
					buffer[buffer_offset++] = b->read_unibus_byte(chain + i);
			}

			flags &= ~0x4000;  // buffer no longer busy
			b->write_unibus_word(p_buffers + 0, flags);
		}

		if (ph & 0x2000) {  // END bit
			DOLOG(debug, false, "deqna(tx): packet for %02x:%02x:%02x:%02x:%02x:%02x, type: %04x", 
				buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5],
				(buffer[12] << 8) | buffer[13]);

			if (buffer_offset == 0) {
				DOLOG(warning, false, "deqna(tx): failed transmitting - empty buffer");
			}
			else {
				bool crs08 = registers[7] & 256; // bit 8
				if (crs08 == false) {  // active low
					bool crs09 = registers[7] & 512;
					DOLOG(debug, false, "deqna(tx): %sloopback", crs09 ? "extended " : "");
					queue_rx_packet(buffer, buffer_offset);
				}
				else {  // push on the wire
					int tx_rc = write(dev_fd, buffer, buffer_offset);
					if (tx_rc <= 0) {
						DOLOG(warning, false, "deqna(tx): failed transmitting - device down?");
						break;
					}
				}

				buffer_offset = 0;
			}

			b->write_unibus_word(p_buffers + 4 * 2, 0x2000);  // all good
			b->write_unibus_word(p_buffers + 5 * 2, 0);  // TDR

			registers[7] |= 128;  // XI
			queued = true;
			if (registers[7] & 64) {  // IE
				uint16_t vector = registers[6] & 0x3fc;
				DOLOG(debug, false, "deqna(tx): packet sent, trigger %06o", vector);
				b->getCpu()->queue_interrupt(DEQNA_IRQ_LEVEL, vector);
			}
		}

		if ((ph & 0x8000) == 0)
			break;

		if (ph & 0x4000)
			p_buffers = chain;
		else
			p_buffers += 12;
	}

	registers[7] |= 16;

	if (!queued)
		DOLOG(debug, false, "deqna(tx): packet NOT queued");
}

void deqna::reset(const bool hard)
{
	DOLOG(debug, false, "deqna %s reset", hard ? "hard" : "soft");

	if (hard) {
		for(int i=0; i<8; i++)
			registers[i] = 0;
		registers[6] = 0774;
		registers[7] = 
			1  |  // software reset
			16 |  // transmit list invalid
			32 |  // receive list invalid
			0x1000;  // power ok
	}

	purge_buffers();
}

void deqna::show_state(console *const cnsl) const
{
	my_unique_lock lck(&lock);
	cnsl->put_string_lf(format("%zu packets queued", received.size()));
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
		uint16_t new_csr = v;

		if (v & 32768)  // clear RI
			new_csr &= ~32768;
		if (v & 128)  // clear TI
			new_csr &= ~128;
		if (v & 2)
			new_csr &= ~2;  // ignore software reset

		new_csr &= ~0x7834;  // these are read only
		new_csr |= old_v & 0x7834;  // copy from old set

		new_csr &= ~0x0800;  // reserved bit

		registers[reg_nr] = new_csr;
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
