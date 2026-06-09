#include <string>

#include "my_lock.h"


class bus;

class ddp {
private:
        std::string server;
	int         n_pixels   { 0 };
        uint8_t     udp_port   { 0 };
	int         brightness { 0 };
	my_lock     lock;

public:
        ddp();
        ~ddp();

        bool begin();
        bool set_target(const std::string & ip, const int n_pixels);
	void push(console *cnsl, bus *const b, const uint8_t brightness);
        void test();
};
