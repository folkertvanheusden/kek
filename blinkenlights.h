#include <map>
#include <string>

class  bus;
struct control_info;

class blinkenlights {
private:
	std::string server;
	int         udp_port;

	// panel, control-name, control-mea
	std::map<std::string, std::map<std::string, control_info> > controls;

public:
	blinkenlights();
	~blinkenlights();

	bool begin();
	bool set_target(const std::string & ip);
	void push(bus *const b);
};
