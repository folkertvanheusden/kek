// (C) 2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#if WITH_PPS
#include <atomic>
#include <thread>

#include "comm.h"
#include "my_lock.h"


class comm_pst: public comm
{
private:
	const std::string dev_name;
	std::atomic_bool  stop_flag { false   };
	std::thread      *th        { nullptr };
	my_lock           msg_buffer_lock;
	std::string       msg_buffer;

public:
	comm_pst(const std::string & dev_name);
	virtual ~comm_pst();

	bool    begin() override;

#if IS_POSIX
	JsonDocument serialize() const override;
	static comm_pst *deserialize(const JsonVariantConst j);
#endif

	std::string get_identifier() const;

	bool    is_connected() override;

	bool    has_data() override;
	uint8_t get_byte() override;

	void    operator()();

	void    send_data(const uint8_t *const in, const size_t n) override;
};
#endif
