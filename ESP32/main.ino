// (C) 2018-2023 by Folkert van Heusden
// Released under MIT license

#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#include <HardwareSerial.h>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <WiFi.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "console_esp32.h"
#include "cpu.h"
#include "debugger.h"
#include "disk_backend.h"
#include "disk_backend_esp32.h"
#include "disk_backend_nbd.h"
#include "error.h"
#include "esp32.h"
#include "gen.h"
#include "kw11-l.h"
#include "loaders.h"
#include "memory.h"
#include "tty.h"
#include "utils.h"


constexpr const char NET_DISK_CFG_FILE[] = "/net-disk.json";

constexpr const char SERIAL_CFG_FILE[] = "/serial.json";

#define MAX_CFG_SIZE 1024
StaticJsonDocument<MAX_CFG_SIZE> json_doc;


bus     *b    = nullptr;
cpu     *c    = nullptr;
tty     *tty_ = nullptr;
console *cnsl = nullptr;

uint16_t exec_addr = 0;

SdFat32  sd;

std::atomic_uint32_t stop_event      { EVENT_NONE };

std::atomic_bool    *running         { nullptr };

bool                 trace_output    { false };

HardwareSerial       Serial_RS232(2);

// std::atomic_bool on_wifi   { false };

void console_thread_wrapper_panel(void *const c)
{
	console *const cnsl = reinterpret_cast<console *>(c);

	cnsl->panel_update_thread();
}

void console_thread_wrapper_io(void *const c)
{
	console *const cnsl = reinterpret_cast<console *>(c);

	cnsl->operator()();
}

uint32_t load_serial_speed_configuration()
{
	File dataFile = LittleFS.open(SERIAL_CFG_FILE, "r");

	if (!dataFile)
		return 115200;

	size_t size = dataFile.size();

	uint8_t buffer[4] { 0 };

	dataFile.read(buffer, 4);

	dataFile.close();

	return (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
}

bool save_serial_speed_configuration(const uint32_t bps)
{
	File dataFile = LittleFS.open(SERIAL_CFG_FILE, "w");

	if (!dataFile)
		return false;

	const uint8_t buffer[] = { uint8_t(bps >> 24), uint8_t(bps >> 16), uint8_t(bps >> 8), uint8_t(bps) };

	dataFile.write(buffer, 4);

	dataFile.close();

	return true;
}

typedef enum { DT_RK05, DT_RL02 } disk_type_t;

std::optional<std::pair<std::vector<disk_backend *>, std::vector<disk_backend *> > > load_disk_configuration(console *const c)
{
	File dataFile = LittleFS.open(NET_DISK_CFG_FILE, "r");

	if (!dataFile)
		return { };

	size_t size = dataFile.size();

	char buffer[MAX_CFG_SIZE];

	if (size > sizeof buffer) {  // this should not happen
		dataFile.close();

		return { };
	}

	dataFile.read(reinterpret_cast<uint8_t *>(buffer), size);
	buffer[(sizeof buffer) - 1] = 0x00;

	dataFile.close();

	auto error = deserializeJson(json_doc, buffer);

	if (error)  // this should not happen
		return { };

	String nbd_host = json_doc["NBD-host"];
	int    nbd_port = json_doc["NBD-port"];

	String disk_type_temp = json_doc["disk-type"];

	disk_type_t disk_type = DT_RK05;

	if (disk_type_temp == "rl02")
		disk_type = DT_RL02;

	disk_backend *d = new disk_backend_nbd(nbd_host.c_str(), nbd_port);

	if (d->begin() == false) {
		c->put_string_lf("Cannot initialize NBD client from configuration file");
		delete d;
		return { };
	}

	c->put_string_lf(format("Connection to NBD server at %s:%d success", nbd_host.c_str(), nbd_port));

	if (disk_type == DT_RK05)
		return { { { d }, { } } };

	if (disk_type == DT_RL02)
		return { { { }, { d } } };

	return { };
}

bool save_disk_configuration(const std::string & nbd_host, const int nbd_port, const disk_type_t dt)
{
	json_doc["NBD-host"] = nbd_host;
	json_doc["NBD-port"] = nbd_port;

	json_doc["disk-type"] = dt == DT_RK05 ? "rk05" : "rl02";

	File dataFile = LittleFS.open(NET_DISK_CFG_FILE, "w");

	if (!dataFile)
		return false;

	serializeJson(json_doc, dataFile);

	dataFile.close();

	return true;
}

typedef enum { BE_NETWORK, BE_SD } disk_backend_t;
std::optional<disk_backend_t> select_disk_backend(console *const c)
{
	c->put_string("1. network (NBD), 2. local SD card, 9. abort");

	int ch = -1;
	while(ch == -1 && ch != '1' && ch != '2' && ch != '9') {
		auto temp = c->wait_char(500);

		if (temp.has_value())
			ch = temp.value();
	}

	c->put_string_lf(format("%c", ch));

	if (ch == '9')
		return { };

	if (ch == '1')
		return BE_NETWORK;

//	if (ch == '2')
		return BE_SD;
}

std::optional<disk_type_t> select_disk_type(console *const c)
{
	c->put_string("1. RK05, 2. RL02, 9. abort");

	int ch = -1;
	while(ch == -1 && ch != '1' && ch != '2' && ch != '9') {
		auto temp = c->wait_char(500);

		if (temp.has_value())
			ch = temp.value();
	}

	c->put_string_lf(format("%c", ch));

	if (ch == '9')
		return { };

	if (ch == '1')
		return DT_RK05;

//	if (ch == '2')
		return DT_RL02;
}

std::optional<std::pair<std::vector<disk_backend *>, std::vector<disk_backend *> > > select_nbd_server(console *const c)
{
	c->flush_input();

	std::string hostname = c->read_line("Enter hostname (or empty to abort): ");

	if (hostname.empty())
		return { };

	std::string port_str = c->read_line("Enter port number (or empty to abort): ");

	if (port_str.empty())
		return { };

	auto disk_type = select_disk_type(c);

	if (disk_type.has_value() == false)
		return { };

	disk_backend *d = new disk_backend_nbd(hostname, atoi(port_str.c_str()));

	if (d->begin() == false) {
		c->put_string_lf("Cannot initialize NBD client");
		delete d;
		return { };
	}

	if (save_disk_configuration(hostname, atoi(port_str.c_str()), disk_type.value()))
		c->put_string_lf("NBD disk configuration saved");
	else
		c->put_string_lf("NBD disk configuration NOT saved");

	if (disk_type.value() == DT_RK05)
		return { { { d }, { } } };

	if (disk_type.value() == DT_RL02)
		return { { { }, { d } } };

	return { };
}

// RK05, RL02 files
std::optional<std::pair<std::vector<disk_backend *>, std::vector<disk_backend *> > > select_disk_files(console *const c)
{
	c->debug("MISO: %d", int(MISO));
	c->debug("MOSI: %d", int(MOSI));
	c->debug("SCK : %d", int(SCK ));
	c->debug("SS  : %d", int(SS  ));

	c->put_string_lf("Files on SD-card:");

	if (!sd.begin(SS, SD_SCK_MHZ(15)))
		sd.initErrorHalt();

	for(;;) {
		sd.ls("/", LS_DATE | LS_SIZE | LS_R);

		c->flush_input();

		std::string selected_file = c->read_line("Enter filename (or empty to abort): ");

		if (selected_file.empty())
			return { };

		auto disk_type = select_disk_type(c);

		if (disk_type.has_value() == false)
			return { };

		c->put_string("Opening file: ");
		c->put_string_lf(selected_file.c_str());

		File32 fh;

		if (fh.open(selected_file.c_str(), O_RDWR)) {
			fh.close();

			disk_backend *temp = new disk_backend_esp32(selected_file);

			if (!temp->begin()) {
				c->put_string("Cannot use: ");
				c->put_string_lf(selected_file.c_str());

				delete temp;

				continue;
			}

			if (disk_type.value() == DT_RK05)
				return { { { temp }, { } } };

			if (disk_type.value() == DT_RL02)
				return { { { }, { temp } } };
		}

		c->put_string_lf("open failed");
	}
}

void set_disk_configuration(std::pair<std::vector<disk_backend *>, std::vector<disk_backend *> > & disk_files)
{
	if (disk_files.first.empty() == false)
		b->add_rk05(new rk05(disk_files.first, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

	if (disk_files.second.empty() == false)
		b->add_rl02(new rl02(disk_files.second, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

	// TODO: allow bootloader to be selected
	if (disk_files.first.empty() == false)
		setBootLoader(b, BL_RK05);
	else
		setBootLoader(b, BL_RL02);
}

void configure_disk(console *const c)
{
	for(;;) {
		Serial.println(F("Load disk"));

		auto backend = select_disk_backend(cnsl);

		if (backend.has_value() == false)
			break;

		std::optional<std::pair<std::vector<disk_backend *>, std::vector<disk_backend *> > > files;

		if (backend == BE_NETWORK)
			files = select_nbd_server(cnsl);
		else // if (backend == BE_SD)
			files = select_disk_files(cnsl);

		if (files.has_value() == false)
			break;

		set_disk_configuration(files.value());

		break;
	}
}

void set_hostname()
{
	WiFi.setHostname("PDP-11");
}

void configure_network(console *const c)
{
	WiFi.persistent(true);

	WiFi.setAutoReconnect(true);

	WiFi.mode(WIFI_STA);

	c->put_string_lf("Scanning for wireless networks...");

	int n_ssids = WiFi.scanNetworks();

	c->put_string_lf("Wireless networks:");

	for(int i=0; i<n_ssids; i++)
		c->put_string_lf(format("\t%s", WiFi.SSID(i).c_str()));

	c->flush_input();

	std::string wifi_ap = c->read_line("Enter SSID[|PSK]: ");

	auto parts = split(wifi_ap, "|");

	if (parts.size() > 2) {
		c->put_string_lf("Invalid SSID/PSK: should not contain '|'");
		return;
	}

	if (parts.size() == 1)
		WiFi.begin(parts.at(0).c_str());
	else
		WiFi.begin(parts.at(0).c_str(), parts.at(1).c_str());
}

void wait_network(console *const c)
{
	constexpr const int timeout = 10 * 3;

	int i = 0;

	while (WiFi.waitForConnectResult() != WL_CONNECTED && i < timeout) {
		c->put_string(".");

		delay(1000 / 3);

		i++;
	}

	if (i == timeout)
		c->put_string_lf("Time out connecting");
}

void check_network(console *const c)
{
	wait_network(c);

	c->put_string_lf("");
	c->put_string_lf(format("Local IP address: %s", WiFi.localIP().toString().c_str()));
}

void start_network(console *const c)
{
	set_hostname();

	WiFi.mode(WIFI_STA);

	WiFi.begin();

	wait_network(c);

	c->put_string_lf("");
	c->put_string_lf(format("Local IP address: %s", WiFi.localIP().toString().c_str()));
}

void set_tty_serial_speed(console *const c, const uint32_t bps)
{
	Serial_RS232.begin(bps);

	if (save_serial_speed_configuration(bps) == false)
		c->put_string_lf("Failed to store configuration file with serial settings");
}

void recall_configuration(console *const c)
{
	c->put_string_lf("Starting network...");
	start_network(cnsl);

	auto disk_configuration = load_disk_configuration(c);

	if (disk_configuration.has_value()) {
		c->put_string_lf("Starting disk...");
		set_disk_configuration(disk_configuration.value());
	}
}

void setup()
{
	Serial.begin(115200);

	Serial.println(F("This PDP-11 emulator is called \"kek\" (reason for that is forgotten) and was written by Folkert van Heusden."));

	Serial.println(F("Build on: " __DATE__ " " __TIME__));

	Serial.print(F("Size of int: "));
	Serial.println(sizeof(int));

	Serial.print(F("CPU clock frequency (MHz): "));
	Serial.println(getCpuFrequencyMhz());

	if (!LittleFS.begin(true))
		Serial.println(F("LittleFS.begin() failed"));

	Serial.print(F("Free RAM before init (decimal bytes): "));
	Serial.println(ESP.getFreeHeap());

	Serial.println(F("Init bus"));
	b = new bus();

	Serial.println(F("Init CPU"));
	c = new cpu(b, &stop_event);

	Serial.println(F("Connect CPU to BUS"));
	b->add_cpu(c);

	constexpr uint32_t hwSerialConfig = SERIAL_8N1;
	uint32_t bitrate = load_serial_speed_configuration();

	Serial.print(F("Init console, baudrate: "));
	Serial.print(bitrate);
	Serial.println(F("bps"));

	Serial_RS232.begin(bitrate, hwSerialConfig, 16, 17);
	Serial_RS232.setHwFlowCtrlMode(0);

	Serial_RS232.println(F("\014Console enabled on TTY"));

	std::vector<Stream *> serial_ports { &Serial_RS232, &Serial };
	cnsl = new console_esp32(&stop_event, b, serial_ports);

	Serial.println(F("Start line-frequency interrupt"));
	kw11_l *lf = new kw11_l(b, cnsl);

	running = cnsl->get_running_flag();

	Serial.println(F("Init TTY"));
	tty_ = new tty(cnsl, b);
	Serial.println(F("Connect TTY to bus"));
	b->add_tty(tty_);

	Serial.println(F("Starting panel"));
	xTaskCreate(&console_thread_wrapper_panel, "panel", 2048, cnsl, 1, nullptr);

	xTaskCreate(&console_thread_wrapper_io,    "c-io",  2048, cnsl, 1, nullptr);

	Serial.print(F("Free RAM after init: "));
	Serial.println(ESP.getFreeHeap());

#if !defined(SHA2017)
	pinMode(LED_BUILTIN, OUTPUT);
#endif

	Serial.flush();

	cnsl->start_thread();
}

void loop()
{
	debugger(cnsl, b, &stop_event, false);

	c->reset();
}
