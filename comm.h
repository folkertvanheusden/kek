#include <cstddef>
#include <cstdint>


class comm
{
public:
	comm();
	virtual ~comm();

	virtual bool    is_connected() = 0;

	virtual bool    has_data() = 0;
	virtual uint8_t get_byte() = 0;

	virtual void    send_data(const uint8_t *const in, const size_t n) = 0;
};
