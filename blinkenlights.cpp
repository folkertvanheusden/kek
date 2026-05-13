// written by Folkert van Heusden <folkert@komputilo.nl>
// license under MIT license
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#if !defined(ESP32) && !defined(BUILD_FOR_RP2040)
#include <poll.h>
#endif
#include <string>
#include <unistd.h>
#include <vector>
#if defined(BUILD_FOR_RP2040)
#include <WiFi.h>
#endif
#if defined(ESP32) || defined(BUILD_FOR_RP2040)
#include <WiFiUdp.h>
#if defined(ESP32)
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#endif
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include "blinkenlights.h"
#include "bus.h"
#include "log.h"

#if defined(BUILD_FOR_RP2040)
constexpr const int local_port = 2000;
WiFiUDP udp;
#endif

// this code does not check all the data returned by the server
// because of code size restraints

#define BLINKENLIGHT_RPC_PROC 99

enum auth_type {
	AUTH_NULL       = 0,
	AUTH_UNIX       = 1,
	AUTH_SHORT      = 2,
	AUTH_DES        = 3
};

struct opaque_auth {
	auth_type type;
	uint32_t  size;
};

enum msg_type {
	CALL  = 0,
	REPLY = 1
};

enum reply_stat {
	MSG_ACCEPTED = 0,
	MSG_DENIED   = 1
};

enum accept_stat {
	SUCCESS       = 0, /* RPC executed successfully       */
	PROG_UNAVAIL  = 1, /* remote hasn't exported program  */
	PROG_MISMATCH = 2, /* remote can't support version #  */
	PROC_UNAVAIL  = 3, /* program can't support procedure */
	GARBAGE_ARGS  = 4  /* procedure can't decode params   */
};

enum reject_stat {
	RPC_MISMATCH = 0, /* RPC version number != 2          */
	AUTH_ERROR = 1    /* remote can't authenticate caller */
};

enum auth_stat {
	AUTH_BADCRED      = 1,  /* bad credentials (seal broken) */
	AUTH_REJECTEDCRED = 2,  /* client must begin new session */
	AUTH_BADVERF      = 3,  /* bad verifier (seal broken)    */
	AUTH_REJECTEDVERF = 4,  /* verifier expired or replayed  */
	AUTH_TOOWEAK      = 5   /* rejected for security reasons */
};

struct call_body {
	uint32_t rpcvers;       /* must be equal to two (2) */
	uint32_t prog;
	uint32_t vers;
	uint32_t proc;
	opaque_auth cred;
	opaque_auth verf;
	/* procedure specific parameters start here */
};

struct accepted_reply {
	opaque_auth verf;
	accept_stat stat;
	uint32_t    unknown;  // TODO
	// procedure-specific results start here
	uint32_t    reply_data;
};

struct accepted_reply_failed {
	accepted_reply reply;
	struct {
		unsigned int low;
		unsigned int high;
	} mismatch_info;
};

union rejected_reply
{
	struct {
		unsigned int low;
		unsigned int high;
	} mismatch_info;

	auth_stat stat;
};

union reply_body {
	reply_stat     stat;
	accepted_reply areply;
	rejected_reply rreply;
};

struct rpc_msg {
	uint32_t xid;
	msg_type mtype;
};

struct rpc_msg_call {
	rpc_msg   header;
	call_body cbody;
};

struct rpc_msg_reply {
	rpc_msg    header;
	reply_body rbody;
};

struct mapping {
	uint32_t program;
	uint32_t version;
	uint32_t protocol;   // 6 = TCP, 17 = UDP
	uint32_t port;       // set to 0 in request
};

static bool validate_reply(const rpc_msg_reply *const rmsg, const uint32_t xid)
{
	if (ntohl(rmsg->header.xid) != xid) {
		DOLOG(debug, false, "Unexpected XID: %d", ntohl(rmsg->header.xid));
		return false;
	}

	if (ntohl(rmsg->header.mtype) != REPLY) {
		DOLOG(debug, false, "Not a reply (%d)", ntohl(rmsg->header.mtype));
		return false;
	}

	if (ntohl(rmsg->rbody.stat) != MSG_ACCEPTED) {
		DOLOG(debug, false, "Request not accepted (%d)", ntohl(rmsg->rbody.stat));
		return false;
	}

	if (ntohl(rmsg->rbody.areply.stat) != SUCCESS) {
		DOLOG(debug, false, "Command error: %d", ntohl(rmsg->rbody.areply.stat));
		return false;
	}

	return true;
}

static const std::pair<const rpc_msg_reply *, int> exchange_message(const std::string & server, const int port, const uint32_t xid, const std::vector<uint8_t> & msg)
{
	constexpr const int max_reply_size = 1500;
	uint8_t            *reply          = nullptr;
	int                 packet_size    = 0;

#if defined(BUILD_FOR_RP2040)
	udp.begin(local_port);
	udp.beginPacket(server.c_str(), port);
	udp.write(msg.data(), msg.size());
	udp.endPacket();

	auto start = millis();
	while(millis() - start < 1000) {
		int packet_size = udp.parsePacket();
		if (packet_size > 0) {
			reply = new uint8_t[packet_size];
			if (!reply) {
				DOLOG(ll_critical, true, "malloc issue");
				return { nullptr, 0 };
			}
			udp.read(reply, packet_size);
			break;
		}
	}

	if (!reply) {
		DOLOG(debug, false, "Timeout waiting for RPC reply");
		return { nullptr, 0 };
	}
#else
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) {
		DOLOG(debug, false, "Cannot create socket: %s", strerror(errno));
		return { nullptr, 0 };
	}

	sockaddr_in serveraddr { };
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port   = htons(port);
	if (inet_aton(server.c_str(), &serveraddr.sin_addr) == 0) {
		DOLOG(debug, false, "inet_aton(%s) failed", server.c_str());
		close(fd);
		return { nullptr, 0 };
	}

	if (sendto(fd, msg.data(), msg.size(), 0, reinterpret_cast<const sockaddr *>(&serveraddr), sizeof serveraddr) == -1) {
		close(fd);
		DOLOG(debug, false, "sendto failed: %s", strerror(errno));
		return { nullptr, 0 };
	}

        pollfd fds[] { { fd, POLLIN, 0 } };
        int poll_rc = poll(fds, 1, 100);
	if (poll_rc <= 0) {
		close(fd);
		if (poll_rc == -1)
			DOLOG(debug, false, "poll failed: %s", strerror(errno));
		else
			DOLOG(debug, false, "blinkenpanel (%s:%d) did not respond to request", server.c_str(), port);
		return { nullptr, 0 };
	}

	reply = new uint8_t[max_reply_size];
	if (!reply) {
		DOLOG(ll_critical, true, "malloc issue: %s", strerror(errno));
		close(fd);
		return { nullptr, 0 };
	}
	packet_size = recv(fd, reply, max_reply_size, 0);
	close(fd);
#endif
	if (packet_size <= 0) {
		DOLOG(debug, false, "recv failed: %s", strerror(errno));
		delete [] reply;
		return { nullptr, 0 };
	}
	if (packet_size % 4) {
		DOLOG(debug, false, "invalid message length (%d)", packet_size);
		delete [] reply;
		return { nullptr, 0 };
	}

	const rpc_msg_reply *rmsg = reinterpret_cast<const rpc_msg_reply *>(reply);

	if (validate_reply(rmsg, xid) == false) {
		delete [] reply;
		return { nullptr, 0 };
	}

	return { rmsg, packet_size / 4 };
}

static void free_rpc_msg_reply(const rpc_msg_reply *p)
{
	delete [] reinterpret_cast<const uint8_t *>(p);
}

static std::vector<uint8_t> encapsulate_rpc_msg(const rpc_msg *const msg, const int n_msg_words)
{
	const uint32_t *const p = reinterpret_cast<const uint32_t *>(msg);

	std::vector<uint8_t> output;
	for(int i=0; i<n_msg_words; i++) {
		output.push_back(p[i] >> 24);
		output.push_back(p[i] >> 16);
		output.push_back(p[i] >>  8);
		output.push_back(p[i]      );
	}

	return output;
}

static std::vector<uint8_t> encapsulate_rpc_msg(const rpc_msg *const msg, const int n_msg_words, const uint8_t *const payload, const int n_payload_bytes)
{
	const uint32_t *const p = reinterpret_cast<const uint32_t *>(msg);

	std::vector<uint8_t> output;
	for(int i=0; i<n_msg_words; i++) {
		output.push_back(p[i] >> 24);
		output.push_back(p[i] >> 16);
		output.push_back(p[i] >>  8);
		output.push_back(p[i]      );
	}

	if (n_payload_bytes) {
		size_t cur_size = output.size();
		output.resize(cur_size + n_payload_bytes);
		memcpy(&output.data()[cur_size], payload, n_payload_bytes);
//		printf("%d %zu %zu", n_payload_bytes, cur_size, output.size());
	}

	return output;
}

static std::vector<uint8_t> encapsulate_rpc_msg(const rpc_msg *const msg, const int n_msg_words, const uint32_t *const payload, const int n_payload_words)
{
        const uint32_t *const p = reinterpret_cast<const uint32_t *>(msg);

        std::vector<uint8_t> output((n_msg_words + n_payload_words) * 4);
        size_t offset = 0;
        for(int i=0; i<n_msg_words; i++) {
                output[offset++] = p[i] >> 24;
                output[offset++] = p[i] >> 16;
                output[offset++] = p[i] >>  8;
                output[offset++] = p[i];
        }

        for(int i=0; i<n_payload_words; i++) {
                output[offset++] = payload[i] >> 24;
                output[offset++] = payload[i] >> 16;
                output[offset++] = payload[i] >>  8;
                output[offset++] = payload[i];
        }

        return output;
}

static std::optional<int> find_port_for_rpc_service(const std::string & server, const int rpc_service)
{
	uint32_t     xid = rand();
	rpc_msg_call msg { };
	msg.header.xid      = xid;
	msg.header.mtype    = CALL;
	msg.cbody.rpcvers   = 2;
	msg.cbody.prog      = 100000;
	msg.cbody.vers      = 2;
	msg.cbody.proc      = 3;  //  getport

	mapping map { uint32_t(rpc_service), 3, 17, 0 };

	auto encapsulated = encapsulate_rpc_msg(reinterpret_cast<const rpc_msg *>(&msg), sizeof(msg) / 4, reinterpret_cast<uint32_t *>(&map), sizeof(map) / 4);
	auto rmsg         = exchange_message(server, 111, xid, encapsulated);
	if (rmsg.first == nullptr)
		return { };
	if (rmsg.second != 7) {
		DOLOG(debug, false, "message invalid size (%d)", rmsg.second);
		return { };
	}

	int port = ntohl(rmsg.first->rbody.areply.reply_data);
	free_rpc_msg_reply(rmsg.first);

	return port;
}

struct rpc_blinkenlight_api_infostringtype {
	uint32_t length;
	char str[1];
};

struct rpc_blinkenlight_api_getinfo_res {
        uint32_t error_code; /* 0 = OK */
        rpc_blinkenlight_api_infostringtype info; /* multi line string */
};

static std::optional<std::string> get_blinkenlight_info(const std::string & server, const int port)
{
	uint32_t     xid = rand();
	rpc_msg_call msg { };
	msg.header.xid      = xid;
	msg.header.mtype    = CALL;
	msg.cbody.rpcvers   = 2;
	msg.cbody.prog      = BLINKENLIGHT_RPC_PROC;
	msg.cbody.vers      = 1;
	msg.cbody.proc      = 1;  // getinfo

	auto encapsulated = encapsulate_rpc_msg(reinterpret_cast<const rpc_msg *>(&msg), sizeof(msg) / 4);
	auto rmsg         = exchange_message(server, port, xid, encapsulated);
	if (rmsg.first == nullptr)
		return { };
	const rpc_blinkenlight_api_getinfo_res *data = reinterpret_cast<const rpc_blinkenlight_api_getinfo_res *>(&rmsg.first->rbody.areply.reply_data);
	if (ntohl(data->error_code)) {
		DOLOG(debug, false, "GETINFO returned error (%d)", ntohl(data->error_code));
		free_rpc_msg_reply(rmsg.first);
		return { };
	}

	// this will fail/crash when the blinkenlight-server returns an invalid length
	std::string rc = std::string(data->info.str, ntohl(data->info.length));
	free_rpc_msg_reply(rmsg.first);

	return rc;
}

std::pair<std::string, const uint32_t *> get_string_and_offset(const uint32_t *const in)
{
	uint32_t    len = ntohl(*in);
	std::string rc  = std::string(reinterpret_cast<const char *>(in + 1), len);
	return { rc, &in[(len + 4 + 3) / 4] };
}

struct control_counts {
	uint32_t panel_nr;
	uint32_t inputs_count;
	uint32_t outputs_count;
};

static std::optional<std::map<std::string, control_counts> > get_blinkenlight_panelinfo(const std::string & server, const int port)
{
	std::map<std::string, control_counts> out;
	uint32_t     xid = rand();
	rpc_msg_call msg { };
	msg.header.xid      = xid;
	msg.header.mtype    = CALL;
	msg.cbody.rpcvers   = 2;
	msg.cbody.prog      = BLINKENLIGHT_RPC_PROC;
	msg.cbody.vers      = 1;
	msg.cbody.proc      = 2;  // getpanelinfo

	std::vector<uint32_t> payload(1);  // panel nr., '0'

	for(;;) {
		auto encapsulated = encapsulate_rpc_msg(reinterpret_cast<const rpc_msg *>(&msg), sizeof(msg) / 4, payload.data(), payload.size());
		auto rmsg         = exchange_message(server, port, xid, encapsulated);
		if (rmsg.first == nullptr)
			return { };
		uint32_t their_rc = ntohl(rmsg.first->rbody.areply.reply_data);
		if (their_rc) {
			free_rpc_msg_reply(rmsg.first);
			break;
		}

		auto           name_p = get_string_and_offset(&rmsg.first->rbody.areply.reply_data + 1);
		control_counts cc     = { ntohl(payload[0]), ntohl(name_p.second[0]), ntohl(name_p.second[1]) };
		free_rpc_msg_reply(rmsg.first);

		out[name_p.first] = cc;
		payload[0] = htonl(ntohl(payload[0]) + 1);
	}

	return { out };
}

struct control_info {
	uint32_t panel_nr;
	uint32_t control_nr;
	uint32_t is_input;
	uint32_t type;
	uint32_t radix;
	uint32_t value_bitlen;
	uint32_t value_bytelen; /* count for value transmission */
	uint64_t value;
};

static std::optional<std::pair<std::string, control_info> > get_blinkenlight_controlinfo(const std::string & server, const int port, const uint32_t panel, const uint32_t control)
{
	uint32_t     xid = rand();
	rpc_msg_call msg { };
	msg.header.xid      = xid;
	msg.header.mtype    = CALL;
	msg.cbody.rpcvers   = 2;
	msg.cbody.prog      = BLINKENLIGHT_RPC_PROC;
	msg.cbody.vers      = 1;
	msg.cbody.proc      = 3;  // control info

	std::vector<uint32_t> payload(2);
	payload[0] = panel;
	payload[1] = control;

	auto encapsulated = encapsulate_rpc_msg(reinterpret_cast<const rpc_msg *>(&msg), sizeof(msg) / 4, payload.data(), payload.size());
	auto rmsg         = exchange_message(server, port, xid, encapsulated);
	if (rmsg.first == nullptr)
		return { };
	uint32_t their_rc = ntohl(rmsg.first->rbody.areply.reply_data);
	if (their_rc) {
		DOLOG(debug, false, "GETCONTROLINFO error: %u", their_rc);
		free_rpc_msg_reply(rmsg.first);
		return { };
	}

	auto     name_p    = get_string_and_offset(&rmsg.first->rbody.areply.reply_data + 1);
	uint32_t is_input  = ntohl(name_p.second[0]);
	uint32_t type      = ntohl(name_p.second[1]);
	uint32_t radix     = ntohl(name_p.second[2]);
	uint32_t bit_len   = ntohl(name_p.second[3]);
	uint32_t byte_len  = ntohl(name_p.second[4]);
	free_rpc_msg_reply(rmsg.first);

	return { { name_p.first, { panel, control, is_input, type, radix, bit_len, byte_len, 0 } } };
}

static void get_blinkenlight_controls(const std::string & server, const int port, const uint32_t panel_nr)
{
	uint32_t     xid = rand();
	rpc_msg_call msg { };
	msg.header.xid      = xid;
	msg.header.mtype    = CALL;
	msg.cbody.rpcvers   = 2;
	msg.cbody.prog      = BLINKENLIGHT_RPC_PROC;
	msg.cbody.vers      = 1;
	msg.cbody.proc      = 5;  // get values

	std::vector<uint32_t> payload(1);
	payload[0] = htonl(panel_nr);

	auto encapsulated = encapsulate_rpc_msg(reinterpret_cast<const rpc_msg *>(&msg), sizeof(msg) / 4, payload.data(), payload.size());
	auto rmsg         = exchange_message(server, port, xid, encapsulated);
	if (!rmsg.first)
		return;

	uint32_t their_rc = ntohl(rmsg.first->rbody.areply.reply_data);
	if (their_rc)
		DOLOG(debug, false, "GETCONTROLVALUE error: %u", their_rc);
	// const uint32_t *p = reinterpret_cast<const uint32_t *>(rmsg.first);
	// TODO return statuses
	free_rpc_msg_reply(rmsg.first);
}

static bool set_blinkenlight_controls(const std::string & server, const int port, const uint32_t panel_nr, const std::map<std::string, control_info> & controls)
{
	uint32_t     xid = rand();
	rpc_msg_call msg { };
	msg.header.xid      = xid;
	msg.header.mtype    = CALL;
	msg.cbody.rpcvers   = 2;
	msg.cbody.prog      = BLINKENLIGHT_RPC_PROC;
	msg.cbody.vers      = 1;
	msg.cbody.proc      = 4;  // set values

	// get outputs
	std::vector<control_info> selection;
	uint32_t sum = 0;
	for(auto & control: controls) {
		if (control.second.type == 2 && control.second.panel_nr == panel_nr) {
			selection.push_back(control.second);
			sum += control.second.value_bytelen;
		}
	}

	// sort by number (required?)
	std::sort(selection.begin(), selection.end(), [](const control_info & a, const control_info & b) { return a.control_nr < b.control_nr; });
	size_t selection_size = selection.size();

	// create payload data
	std::vector<uint8_t> control_stream(12 + sum * 4);
	control_stream[4] = panel_nr >> 24;
	control_stream[5] = panel_nr >> 16;
	control_stream[6] = panel_nr >>  8;
	control_stream[7] = panel_nr;
	size_t offset = 12;
	auto   it     = selection.begin();
	for(size_t i=0; i<selection_size; i++, it++) {
		uint32_t value  = it->value;
		control_stream[offset + 3] = value;
		offset += 4;
		if (i < 2) {
			control_stream[offset + 3] = value >> 8;
			offset += 4;
			if (i == 0) {
				control_stream[offset + 3] = value >> 16;
				offset += 4;
			}
		}
	}
	control_stream[8] = sum >> 24;
	control_stream[9] = sum >> 16;
	control_stream[10] = sum >> 8;
	control_stream[11] = sum;

	auto encapsulated = encapsulate_rpc_msg(reinterpret_cast<const rpc_msg *>(&msg), sizeof(msg) / 4, control_stream.data(), control_stream.size());
	auto rmsg         = exchange_message(server, port, xid, encapsulated);
	if (rmsg.first == nullptr)
		return { };
	uint32_t their_rc = ntohl(rmsg.first->rbody.areply.reply_data);
	if (their_rc)
		DOLOG(debug, false, "SETCONTROLVALUES error: %u", their_rc);
	free_rpc_msg_reply(rmsg.first);

	return their_rc == 0;
}

blinkenlights::blinkenlights()
{
}

blinkenlights::~blinkenlights()
{
}

bool blinkenlights::begin()
{
	return true;
}

bool blinkenlights::set_target(const std::string & ip)
{
	my_unique_lock lck(&controls_lock);
	valid  = false;
	server = ip;

	auto rc_portmap = find_port_for_rpc_service(server, BLINKENLIGHT_RPC_PROC);
	if (rc_portmap.has_value() == false) {
		DOLOG(ll_error, false, "did not return rpc udp port");
		return false;
	}

	udp_port = rc_portmap.value();

	DOLOG(info, false, "Blinkenlight is on port %d", udp_port);

	auto rc_info = get_blinkenlight_info(server, udp_port);
	if (rc_info.has_value()) {
		DOLOG(info, false, "Info:");
		auto lines = split(rc_info.value(), "\n");
		for(auto & line: lines)
			DOLOG(info, false, " %s", line.c_str());
	}

	controls.clear();

	auto rc_panelsinfo = get_blinkenlight_panelinfo(server, udp_port);
	if (rc_panelsinfo.has_value()) {
		for(auto & panel: rc_panelsinfo.value()) {
			DOLOG(info, false, "Panel name: %s, # input: %u, # output: %u", panel.first.c_str(), panel.second.inputs_count, panel.second.outputs_count);

			controls[panel.first] = { };
			for(unsigned i=0; i<panel.second.inputs_count + panel.second.outputs_count; i++) {
				auto rc = get_blinkenlight_controlinfo(server, udp_port, panel.second.panel_nr, i);
				if (rc.has_value()) {
					controls[panel.first][rc.value().first] = rc.value().second;
					DOLOG(debug, false, " %s (%u|%u, radix: %u, bytes: %u, bits: %u)", rc.value().first.c_str(),
							rc.value().second.type, rc.value().second.is_input,
							rc.value().second.radix, rc.value().second.value_bytelen, rc.value().second.value_bitlen);
				}
			}
		}

		valid = controls.find("11/70") != controls.end();  // crude check
	}
	else {
		DOLOG(warning, false, "Device has no panels nor controls");
		return false;
	}

	return true;
}

void blinkenlights::push(bus *const b, const bool running_flag)
{
	my_unique_lock lck(&controls_lock);
	if (!valid)
		return;
	auto panel             = controls.find("11/70");
	try {
		auto address_control   = panel->second.find("ADDRESS"  );
		auto data_control      = panel->second.find("DATA"     );
		auto mmr0_control      = panel->second.find("MMR0_MODE");
		auto run_control       = panel->second.find("RUN"      );

		cpu *const c           = b->getCpu();

		uint16_t current_PSW   = c->getPSW();
		int      run_mode      = current_PSW >> 14;
		uint16_t current_PC    = c->getPC();
		memory_addresses_t rc  = b->getMMU()->calculate_physical_address(run_mode, current_PC);
		auto     current_instr = b->peek_word(run_mode, current_PC);

		address_control->second.value = rc.physical_data;
		data_control   ->second.value = current_instr.has_value() ? current_instr.value() : 0;
		if (run_mode == 0)  // kernel
			mmr0_control->second.value = 1;
		else if (run_mode == 2)  // super
			mmr0_control->second.value = 2;
		else if (run_mode == 3)  // user
			mmr0_control->second.value = 4;

		run_control->second.value = running_flag;
	}
	catch(int trap_nr) {
		DOLOG(ll_error, false, "Trap %d caught in blinkenlights::push", trap_nr);
		for(auto & control: panel->second)
			control.second.value = 0;
	}
	catch(...) {
		// most likely a find() that failed
		DOLOG(ll_error, false, "Unexpected exception in blinkenlights::push (setup)");
	}

	try {
		set_blinkenlight_controls(server, udp_port, panel->second.begin()->second.panel_nr, panel->second);
		get_blinkenlight_controls(server, udp_port, 0);
	}
	catch(...) {
		DOLOG(ll_error, false, "Unexpected exception in blinkenlights::push (set/get)");
	}
}
