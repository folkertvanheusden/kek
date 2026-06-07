#include <string>

#include "my_lock.h"


class bus;

class ddp {
private:
        std::string server;
        int         udp_port { 0 };
	my_lock     lock;

public:
        ddp();
        ~ddp();

        bool begin();
        bool set_target(const std::string & ip);
        void push(bus *const b, const bool running_flag);
        void test();
};
