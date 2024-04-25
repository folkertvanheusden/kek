#pragma once


class device
{
public:
	device() {
	}

	virtual ~device() {
	}

	virtual void reset() = 0;

        virtual uint8_t  readByte(const uint16_t addr) = 0;
        virtual uint16_t readWord(const uint16_t addr) = 0;

        virtual void writeByte(const uint16_t addr, const uint8_t  v) = 0;
        virtual void writeWord(const uint16_t addr, const uint16_t v) = 0;
};
