// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <errno.h>
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "error.h"
#include "gen.h"
#include "log.h"
#include "rl02.h"
#include "utils.h"


static const char * const regnames[] = { 
	"control status",
	"bus address",
	"disk address",
	"multipurpose"
	};

static const char * const commands[] = {
	"no-op",
	"write check",
	"get status",
	"seek",
	"read header",
	"write data",
	"read data",
	"read data w/o header check"
	};

rl02::rl02(bus *const b, std::atomic_bool *const disk_read_activity, std::atomic_bool *const disk_write_activity) :
	b(b),
	disk_read_activity (disk_read_activity ),
	disk_write_activity(disk_write_activity)
{
	reset();
}

rl02::~rl02()
{
	for(auto fh : fhs)
		delete fh;
}

void rl02::reset()
{
	memset(registers,   0x00, sizeof registers  );
	memset(xfer_buffer, 0x00, sizeof xfer_buffer);
	memset(mpr,         0x00, sizeof mpr        );

	track  = 0;
	head   = 0;
	sector = 0;
}

#if IS_POSIX
json_t *rl02::serialize() const
{
	json_t *j = json_object();

	json_t *j_backends = json_array();
	for(auto & dbe: fhs)
		json_array_append(j_backends, dbe->serialize());

	json_object_set(j, "backends", j_backends);

	for(int regnr=0; regnr<4; regnr++)
		json_object_set(j, format("register-%d", regnr).c_str(), json_integer(registers[regnr]));

	for(int mprnr=0; mprnr<3; mprnr++)
		json_object_set(j, format("mpr-%d", mprnr).c_str(), json_integer(mpr[mprnr]));

	json_object_set(j, "track",  json_integer(track));
	json_object_set(j, "head",   json_integer(head));
	json_object_set(j, "sector", json_integer(sector));

	return j;
}

rl02 *rl02::deserialize(const json_t *const j, bus *const b)
{
	std::vector<disk_backend *> backends;

	rl02 *r = new rl02(b, nullptr, nullptr);

	json_t *j_backends = json_object_get(j, "backends");
	for(size_t i=0; i<json_array_size(j_backends); i++)
		r->access_disk_backends()->push_back(disk_backend::deserialize(json_array_get(j_backends, i)));

	for(int regnr=0; regnr<4; regnr++)
		r->registers[regnr] = json_integer_value(json_object_get(j, format("register-%d", regnr).c_str()));

	for(int mprnr=0; mprnr<3; mprnr++)
		r->mpr[mprnr] = json_integer_value(json_object_get(j, format("mpr-%d", mprnr).c_str()));

	r->track  = json_integer_value(json_object_get(j, "track" ));
	r->head   = json_integer_value(json_object_get(j, "head"  ));
	r->sector = json_integer_value(json_object_get(j, "sector"));

	return r;
}
#endif

uint8_t rl02::readByte(const uint16_t addr)
{
	uint16_t v = readWord(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t rl02::readWord(const uint16_t addr)
{
	const int reg = (addr - RL02_BASE) / 2;

	if (addr == RL02_CSR) {  // control status
		setBit(registers[reg], 0, true);  // drive ready (DRDY)
		setBit(registers[reg], 7, true);  // controller ready (CRDY)
	}

	uint16_t value = 0;

	if (addr == RL02_MPR) {  // multi purpose register
		value = mpr[0];
		mpr[0] = mpr[1];
		mpr[1] = mpr[2];
		mpr[2] = 0;
	}
	else {
		value = registers[reg];
	}

	DOLOG(debug, false, "RL02: read \"%s\"/%o: %06o", regnames[reg], addr, value);

	return value;
}

void rl02::writeByte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - RL02_BASE) / 2];

	if (addr & 1) {
		vtemp &= ~0xff00;
		vtemp |= v << 8;
	}
	else {
		vtemp &= ~0x00ff;
		vtemp |= v;
	}

	writeWord(addr, vtemp);
}

uint32_t rl02::get_bus_address() const
{
	return (registers[(RL02_BAR - RL02_BASE) / 2] | (uint32_t((registers[(RL02_CSR - RL02_BASE) / 2] >> 4) & 3) << 16)) & ~1;
}

void rl02::update_bus_address(const uint32_t a)
{
	registers[(RL02_BAR - RL02_BASE) / 2] = a;

	registers[(RL02_CSR - RL02_BASE) / 2] &= ~(3 << 4);
	registers[(RL02_CSR - RL02_BASE) / 2] |= ((a >> 16) & 3) << 4;
}

uint32_t rl02::calc_offset() const
{
	return (rl02_sectors_per_track * track * 2 + head * rl02_sectors_per_track + sector) * rl02_bytes_per_sector;
}

void rl02::update_dar()
{
	registers[(RL02_DAR - RL02_BASE) / 2] = (sector & 63) | (head << 6) | (track << 7);
}

void rl02::writeWord(const uint16_t addr, uint16_t v)
{
	const int reg = (addr - RL02_BASE) / 2;

	DOLOG(debug, false, "RL02: write \"%s\"/%06o: %06o", regnames[reg], addr, v);

        registers[reg] = v;

	if (addr == RL02_CSR) {  // control status
		const uint8_t command = (v >> 1) & 7;

		const bool    do_exec = !(v & 128);

		int           device  = (v >> 8) & 3;

		DOLOG(debug, false, "RL02: device %d, set command %d, exec: %d (%s)", device, command, do_exec, commands[command]);

		bool          do_int  = false;

		if (size_t(device) >= fhs.size()) {
			DOLOG(info, false, "RL02: PDP11/70 is accessing a not-attached virtual disk %d", device);

			registers[(RL02_CSR - RL02_BASE) / 2] |= (1 << 10) | (1 << 15);

			do_int = true;
		}
		else if (command == 2) {  // get status
			mpr[0] = 5 /* lock on */ | (1 << 3) /* brush home */ | (1 << 4) /* heads over disk */ | (head << 6) | (1 << 7) /* RL02 */;
			mpr[1] = mpr[0];
		}
		else if (command == 3) {  // seek
			uint16_t temp = registers[(RL02_DAR - RL02_BASE) / 2];

			int cylinder_count = (temp >> 7) * (temp & 4 ? 1 : -1);

			int16_t new_track = track + cylinder_count;

			if (new_track < 0)
				new_track = 0;
			else if (new_track >= rl02_track_count)
				new_track = rl02_track_count - 1;

			DOLOG(debug, false, "RL02: device %d, seek from cylinder %d to %d (distance: %d, DAR: %06o)", device, track, new_track, cylinder_count, temp);
			track  = new_track;

//			update_dar();

			do_int = true;
		}
		else if (command == 4) {  // read header
			mpr[0] = (sector & 63) | (head << 6) | (track << 7);
			mpr[1] = 0;  // zero
			mpr[2] = 0;  // TODO: CRC

			DOLOG(debug, false, "RL02: device %d, read header [cylinder: %d, head: %d, sector: %d] %06o", device, track, head, sector, mpr[0]);

			do_int = true;
		}
		else if (command == 5) {  // write data
			if (disk_write_activity)
				*disk_write_activity = true;

			uint32_t memory_address   = get_bus_address();

			uint32_t count            = (65536l - registers[(RL02_MPR - RL02_BASE) / 2]) * 2;
			if (count == 65536)
				count = 0;

			uint16_t temp             = registers[(RL02_DAR - RL02_BASE) / 2];

			sector = temp & 63;
			head   = (temp >> 6) & 1;
			track  = temp >> 7;

			uint32_t temp_disk_offset = calc_offset();

			DOLOG(debug, false, "RL02: device %d, write %d bytes (dec) to %d (dec) from %06o (oct) [cylinder: %d, head: %d, sector: %d]", device, count, temp_disk_offset, memory_address, track, head, sector);

			while(count > 0) {
				uint32_t cur = std::min(uint32_t(sizeof xfer_buffer), count);

				for(uint32_t i=0; i<cur;) {
					// BA and MPR are increased by 2
					xfer_buffer[i++] = b->readUnibusByte(memory_address++);
					xfer_buffer[i++] = b->readUnibusByte(memory_address++);

					// update_bus_address(memory_address);
					mpr[0]++;
				}

				if (fhs.at(device) == nullptr || fhs.at(device)->write(temp_disk_offset, cur, xfer_buffer, 256) == false) {
					DOLOG(ll_error, true, "RL02: write error, device %d, disk offset %u, read size %u, cylinder %d, head %d, sector %d", device, temp_disk_offset, cur, track, head, sector);
					break;
				}

				mpr[0] += count / 2;

				temp_disk_offset += cur;

				count -= cur;

				sector++;
				if (sector >= rl02_sectors_per_track) {
					sector = 0;

					head++;
					if (head >= 2) {
						head = 0;

						track++;
					}
				}
			}

			do_int = true;

			if (disk_write_activity)
				*disk_write_activity = false;
		}
		else if (command == 6 || command == 7) {  // read data / read data without header check
			if (disk_read_activity)
				*disk_read_activity = true;

			uint32_t memory_address   = get_bus_address();

			uint32_t count            = (65536l - registers[(RL02_MPR - RL02_BASE) / 2]) * 2;
			if (count == 65536)
				count = 0;

			uint16_t temp             = registers[(RL02_DAR - RL02_BASE) / 2];

			sector = temp & 63;
			head   = (temp >> 6) & 1;
			track  = temp >> 7;

			uint32_t temp_disk_offset = calc_offset();

			DOLOG(debug, false, "RL02: device %d, read %d bytes (dec) from %d (dec) to %06o (oct) [cylinder: %d, head: %d, sector: %d]", device, count, temp_disk_offset, memory_address, track, head, sector);

//			update_dar();

			while(count > 0) {
				uint32_t cur = std::min(uint32_t(sizeof xfer_buffer), count);

				if (fhs.at(device) == nullptr || fhs.at(device)->read(temp_disk_offset, cur, xfer_buffer, 256) == false) {
					DOLOG(ll_error, true, "RL02: read error, device %d, disk offset %u, read size %u, cylinder %d, head %d, sector %d", device, temp_disk_offset, cur, track, head, sector);
					break;
				}

				for(uint32_t i=0; i<cur;) {
					// BA and MPR are increased by 2
					b->writeUnibusByte(memory_address++, xfer_buffer[i++]);
					b->writeUnibusByte(memory_address++, xfer_buffer[i++]);

					// update_bus_address(memory_address);

					mpr[0]++;
				}

				temp_disk_offset += cur;

				count -= cur;

				sector++;
				if (sector >= rl02_sectors_per_track) {
					sector = 0;

					head++;
					if (head >= 2) {
						head = 0;

						track++;
					}
				}

//				update_dar();
			}

			do_int = true;

			if (disk_read_activity)
				*disk_read_activity = false;
		}
		else {
			DOLOG(debug, false, "RL02: command %d not implemented", command);
		}

		if (do_int) {
			if (registers[(RL02_CSR - RL02_BASE) / 2] & 64) {  // interrupt enable?
				DOLOG(debug, false, "RL02: triggering interrupt");

				b->getCpu()->queue_interrupt(5, 0160);
			}
		}
	}
}
