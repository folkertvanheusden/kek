// reference: https://treasures.scss.tcd.ie/hardware/TCD-SCSS-T.20141120.008/EK-DELQA-UG-002.pdf
#include "gen.h"
#include <cinttypes>
#include <cstring>
#include <unistd.h>

#include "deqna.h"
#include "log.h"
#include "utils.h"


constexpr const uint8_t bc_addr[] { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

#if defined(FREERTOS)
static void thread_wrapper_receiver_low(void *p)
{
	deqna *const deqna_ = reinterpret_cast<deqna *>(p);
	deqna_->receiver_low();
	vTaskDelete(nullptr);
}

static void thread_wrapper_receiver_high(void *p)
{
	deqna *const deqna_ = reinterpret_cast<deqna *>(p);
	deqna_->receiver_high();
	vTaskDelete(nullptr);
}
#endif

deqna::deqna(bus *const b, const uint8_t mac_address[6], eth_transport *const eth_dev, abool *const activity_flag):
	b(b),
	eth_dev(eth_dev),
	activity_flag(activity_flag)
{
	memcpy(this->mac_address, mac_address, sizeof this->mac_address);
	reset(true);
}

bool deqna::begin()
{
#if defined(FREERTOS)
	xTaskCreate(&thread_wrapper_receiver_low,  "deqna-rl", 1024, this, 1, nullptr);
	xTaskCreate(&thread_wrapper_receiver_high, "deqna-rh", 1024, this, 1, nullptr);
#else
	th_rx_low  = new std::thread(&deqna::receiver_low,  this);
	th_rx_high = new std::thread(&deqna::receiver_high, this);
#endif
	return true;
}

deqna::~deqna()
{
	stop_flag = true;
#if defined(FREERTOS)
	while(rx_low_stopped == false || rx_high_stopped == false)
		vTaskDelay(1 / portTICK_PERIOD_MS);
#else
	if (th_rx_low) {
		th_rx_low->join();
		delete th_rx_low;
	}
	if (th_rx_high) {
		th_rx_high->join();
		delete th_rx_high;
	}
#endif
	purge_buffers();
	delete eth_dev;
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
		DOLOG(log_ss::LS_DEQNA, "deqna: rx queue full, packet dropped");
		total_n_rx_drop++;
	}
}

std::string to_hex(const uint8_t *const data, const size_t n_bytes)
{
	std::string out;
	for(auto i=0; i<n_bytes; i++) {
		if (i)
			out += " ";
		out += format("%02x", data[i]);
	}
	return out;
}

void dump_packet(console *const cnsl, const uint8_t *const data, const size_t n_bytes, const bool full)
{
	std::string out;

	if (n_bytes < 14)
		out = to_hex(data, n_bytes);
	else {
		out = to_hex(&data[0], 6) + " < " + to_hex(&data[6], 6) + "|" + to_hex(&data[12], 2);

		if (full)
			out += ": " + to_hex(&data[14], n_bytes - 14);
	}

	cnsl->put_string_lf(out);
}

// receiver pushes packets on a queue and signals another thread
// to process it. this allows loopback
void deqna::receiver_low()
{
	set_thread_name("deqna:rx_low");
	DOLOG(log_ss::LS_DEQNA, "deqna(rxl) LOW RECEIVER THREAD starting");

	while(!stop_flag) {
		auto pkt = eth_dev->get(100);
		if (pkt.first == nullptr)
			continue;

		if (monitor_mode == everything)
			dump_packet(cnsl, pkt.first, pkt.second, true);

		if (pkt.second < 14) {
			DOLOG(log_ss::LS_DEQNA, "deqna(rxl) packet too short (%zu)", pkt.second);
			delete [] pkt.first;
			continue;
		}

		// only for us or broadcast
		if (memcmp(pkt.first, mac_address, 6) == 0 || memcmp(pkt.first, bc_addr, 6) == 0) {
			if (registers[7] & 1) {  // receiver enabled?
				if (monitor_mode == filtered)
					dump_packet(cnsl, pkt.first, pkt.second, false);
				total_n_rx_pkts++;
				DOLOG(log_ss::LS_DEQNA, "deqna(rxl) packet received from real Ethernet");
				queue_rx_packet(pkt.first, pkt.second);
			}
			else {
				DOLOG(log_ss::LS_DEQNA, "deqna(rxl) dropped packet from %02x:%02x:%02x:%02x:%02x:%02x: receiver not enabled",
						pkt.first[6], pkt.first[7],  pkt.first[8],
						pkt.first[9], pkt.first[10], pkt.first[11]);
				total_n_rx_drop++;
			}
		}
		delete [] pkt.first;
	}

#if defined(FREERTOS)
	rx_low_stopped = true;
#endif

	DOLOG(log_ss::LS_DEQNA, "deqna(rxl) LOW RECEIVER THREAD TERMINATING");
}

void deqna::receiver_high()
{
	set_thread_name("deqna:rx_high");
	DOLOG(log_ss::LS_DEQNA, "deqna(rxh) HIGH RECEIVER THREAD starting");

	bool invalid_list_shown = false;
	while(!stop_flag) {
		// receive list invalid?
		if (registers[7] & 32) {
			if (!invalid_list_shown) {
				DOLOG(log_ss::LS_DEQNA, "deqna(rxh): receive list invalid");
				invalid_list_shown = true;
			}
			myusleep(received.is_empty() ? 10000 : 1000);
			continue;
		}

		invalid_list_shown = false;

		auto item = received.pop(100);
		if (item.has_value() == false)
			continue;

		*activity_flag = true;

		const uint8_t *const buffer   = item.value().first;
		const size_t         byte_cnt = item.value().second;

		DOLOG(log_ss::LS_DEQNA, "deqna(rxh): Ethernet packet received (%" PRIzu " bytes, from %02x:%02x:%02x:%02x:%02x:%02x, type: %04x)",
				byte_cnt,
				buffer[6], buffer[7], buffer[8], buffer[9], buffer[10], buffer[11], (buffer[12] << 8) | buffer[13]);

		uint32_t p_buffers = ((registers[3] & 63) << 16) | registers[2];
		DOLOG(log_ss::LS_DEQNA, "deqna(rxh): RBL is at %08o", p_buffers);

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
				DOLOG(log_ss::LS_DEQNA, "deqna(rxh): %08o is an end maker", p_buffers);
				break;
			}
			if ((ph & 0x4000) == 0) {  // chain? no, use as buffer
				DOLOG(log_ss::LS_DEQNA, "deqna(rxh): flags: %06o, ph: %06o, status1: %06o, status2: %06o", b->read_unibus_word(p_buffers + 0 * 2), ph, b->read_unibus_word(p_buffers + 4 * 2), b->read_unibus_word(p_buffers + 5 * 2));
				DOLOG(log_ss::LS_DEQNA, "deqna(rxh): %08o is not a chain pointer, use as buffer-pointer (%d bytes)", chain, length);
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
					DOLOG(log_ss::LS_DEQNA, "deqna(rxh): packet queued, trigger %06o", vector);
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

		if (!queued) {
			total_n_rx_drop++;
			DOLOG(log_ss::LS_DEQNA, "deqna(rxh): packet NOT queued");
		}
	}

#if defined(FREERTOS)
	rx_high_stopped = true;
#endif

	DOLOG(log_ss::LS_DEQNA, "deqna(rxh) HIGH RECEIVER THREAD TERMINATING");
}

void deqna::transmitter()
{
	total_n_tx_pkts++;

	*activity_flag = true;

	// sender list invalid?
	if (registers[7] & 16) {
		total_n_tx_drop++;
		return;
	}

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

		DOLOG(log_ss::LS_DEQNA, "deqna(tx): checking descr at %08o, points to %08o which is %d bytes (0x%04x | %04x)", p_buffers, chain, length, len, ~len);

		if ((ph & 0x8000) == 0) {  // valid?
			DOLOG(log_ss::LS_DEQNA, "deqna(tx): %08o is end of BDL", p_buffers);
			break;
		}
		else {
			b->write_unibus_word(p_buffers + 0, 0xffff);  // buffer busy

			if ((ph & 0x4000) == 0x0000) {  // chain? no, use as buffer
				DOLOG(log_ss::LS_DEQNA, "deqna(tx): %08o is not a chain pointer, use as buffer-pointer", chain);
				if (length > 2048) {
					DOLOG(log_ss::LS_DEQNA, "deqna(tx): buffer has invalid size %d", length);
					break;
				}
				if (chain + length > b->get_memory_size()) {
					DOLOG(log_ss::LS_DEQNA, "deqna(tx): buffer does not fit in RAM");
					break;
				}

				DOLOG(log_ss::LS_DEQNA, "deqna(tx): flags: %06o, ph: %06o, status1: %06o, status2: %06o", flags, ph, b->read_unibus_word(p_buffers + 4 * 2), b->read_unibus_word(p_buffers + 5 * 2));

				for(int i=0; i<length && size_t(buffer_offset) < sizeof(buffer); i++)
					buffer[buffer_offset++] = b->read_unibus_byte(chain + i);
			}

			flags &= ~0x4000;  // buffer no longer busy
			b->write_unibus_word(p_buffers + 0, flags);
		}

		if (ph & 0x2000) {  // END bit
			DOLOG(log_ss::LS_DEQNA, "deqna(tx): packet for %02x:%02x:%02x:%02x:%02x:%02x, type: %04x", 
				buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5],
				(buffer[12] << 8) | buffer[13]);

			if (buffer_offset == 0) {
				DOLOG(log_ss::LS_DEQNA, "deqna(tx): failed transmitting - empty buffer");
			}
			else {
				bool crs08 = registers[7] & 256; // bit 8
				if (crs08 == false) {  // active low
					bool crs09 = registers[7] & 512;
					DOLOG(log_ss::LS_DEQNA, "deqna(tx): %sloopback", crs09 ? "extended " : "");
					queue_rx_packet(buffer, buffer_offset);
				}
				else {  // push on the wire
					if (eth_dev->transmit(buffer, buffer_offset) == false) {
						total_n_tx_fail++;
						DOLOG(log_ss::LS_DEQNA, "deqna(tx): cannot transmit frame");
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
				DOLOG(log_ss::LS_DEQNA, "deqna(tx): packet sent, trigger %06o", vector);
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

	if (!queued) {
		total_n_tx_drop++;
		DOLOG(log_ss::LS_DEQNA, "deqna(tx): packet NOT queued");
	}

	*activity_flag = false;
}

void deqna::reset(const bool hard)
{
	DOLOG(log_ss::LS_DEQNA, "deqna %s reset", hard ? "hard" : "soft");

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
	cnsl->put_string_lf(format("MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]));
	cnsl->put_string_lf(format("%" PRIzu " packets queued", received.aprox_size()));
	for(int i=0; i<8; i++)
		cnsl->put_string_lf(format("reg %d: %06o", i, uint16_t(registers[i])));
	cnsl->put_string_lf(format("rx total  : %6" PRIu64, uint64_t(total_n_rx_pkts)));
	cnsl->put_string_lf(format("rx dropped: %6" PRIu64, uint64_t(total_n_rx_drop)));
	cnsl->put_string_lf(format("tx total  : %6" PRIu64, uint64_t(total_n_tx_pkts)));
	cnsl->put_string_lf(format("tx dropped: %6" PRIu64, uint64_t(total_n_tx_drop)));
	cnsl->put_string_lf(format("tx failed : %6" PRIu64, uint64_t(total_n_tx_fail)));
	cnsl->put_string_lf("Transport: ");
	eth_dev->show_state(cnsl);
}

uint8_t deqna::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr & ~1);
	if (addr & 1)
		return v >> 8;
	return v;
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

	DOLOG(log_ss::LS_DEQNA, "deqna read from %06o (%d): %06o", addr, reg_nr, rc);

	return rc;
}

void deqna::write_byte(const uint16_t addr, const uint8_t v)
{
	// just for completeness: the deqna only supports word-access
	int reg_nr = (addr - DEQNA_BASE) / 2;
	DOLOG(log_ss::LS_DEQNA, "deqna write_b %03o to %06o (%d)", v, addr, reg_nr);
        uint16_t vtemp = registers[reg_nr];
        update_word(&vtemp, addr & 1, v);
        write_word(addr, vtemp);
}

void deqna::write_word(const uint16_t addr, const uint16_t v)
{
	int reg_nr = (addr - DEQNA_BASE) / 2;
	DOLOG(log_ss::LS_DEQNA, "deqna write %06o to %06o (%d)", v, addr, reg_nr);

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

		if (received.is_empty() == false)
			new_csr |= 0x8000;

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

bool deqna::test(console *const cnsl)
{
	if (eth_dev) {
		uint8_t buffer[14 + 44] { };
		memset(&buffer[0], 0xff, 6);
		memcpy(&buffer[6], mac_address, 6);
		buffer[12] = 0x08;  // ARP packet
		buffer[13] = 0x06;
		uint8_t *payload = &buffer[14];
		int hw_size = 6;  // MAC address is 6 bytes
		int p_size  = 4;  // IP4 address is 4 bytes
		payload[1] = 1;  // Ethernet
		payload[2] = 8;  // IP4
		payload[4] = hw_size;
		payload[5] = p_size;
		payload[7] = 1;  // request
		uint16_t sha_offset = 8;
		uint16_t spa_offset = 8 + hw_size;
		uint16_t tha_offset = spa_offset + p_size;
		uint16_t tpa_offset = tha_offset + hw_size;
		memcpy(&payload[sha_offset], mac_address, 6);  // who has 255.255.255.255 tell 255.255.255.255 @ our mac
		for(int i=0; i<4; i++) {
			payload[i + spa_offset] = 255;
			payload[i + tpa_offset] = 255;
		}
		// MAYBE this triggers a reply, probably not

		if (!eth_dev->transmit(buffer, sizeof buffer)) {
			cnsl->put_string_lf("Transmit failed");
			return false;
		}

		// any data? usually there are at least ARP msgs broadcasted
		auto data = eth_dev->get(1000);
		if (data.first) {
			delete [] data.first;
		}
		else {
			cnsl->put_string_lf("No data?");
			return false;
		}

		return true;
	}

	cnsl->put_string_lf("No transport medium configured");
	return false;
}

void get_deqna_mac(uint8_t *const to)
{
	const std::string mac_file = ".deqna_mac.dat";
	uint8_t mac_address[] { 0x08, 0x00, 0x2b, 0, 0, 0 };
#if defined(TEENSY4_1)
	std::string mac_str;
#else
	std::string mac_str = get_configuration_string(mac_file, "");
#endif
	if (mac_str.empty()) {
		for(int i=3; i<6; i++)
			mac_address[i] = rand();
		memcpy(to, mac_address, 6);
		put_configuration_string(mac_file, format("%02x%02x%02x%02x%02x%02x",
					mac_address[0], mac_address[1], mac_address[2],
					mac_address[3], mac_address[4], mac_address[5]));
	}
	else {
		for(int i=0; i<6; i++)
			to[i] = std::stoi(mac_str.substr(i * 2, 2), nullptr, 16);
	}
}
