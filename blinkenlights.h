#include <atomic>
#include <map>
#include <mutex>
#include <string>

class  bus;
struct control_info;

class blinkenlights {
private:
	std::string server;
	int         udp_port    { 0     };
	// panel, control-name, control-mea
	std::mutex  controls_lock;
	bool        valid       { false };
	std::map<std::string, std::map<std::string, control_info> > controls;

public:
	blinkenlights();
	~blinkenlights();

	bool begin();
	bool set_target(const std::string & ip);
	void push(bus *const b, const bool running_flag);
};
