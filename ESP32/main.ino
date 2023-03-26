// (C) 2018-2023 by Folkert van Heusden
// Released under Apache License v2.0
#include <Arduino.h>
#include <atomic>
#include <HardwareSerial.h>
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

typedef enum { BE_NETWORK, BE_SD } disk_backend_t;
std::optional<disk_backend_t> select_disk_backend(console *const c)
{
	c->put_string("1. network (NBD), 2. local SD card, 9. abort");

	int ch = -1;
	while(ch == -1 && ch != '1' && ch != '2' && ch != '9')
		ch = c->wait_char(500);

	c->put_string_lf(format("%c", ch));

	if (ch == '9')
		return { };

	if (ch == '1')
		return BE_NETWORK;

//	if (ch == '2')
		return BE_SD;
}

typedef enum { DT_RK05, DT_RL02 } disk_type_t;

std::optional<disk_type_t> select_disk_type(console *const c)
{
	c->put_string("1. RK05, 2. RL02, 9. abort");

	int ch = -1;
	while(ch == -1 && ch != '1' && ch != '2' && ch != '9')
		ch = c->wait_char(500);

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

		if (files.value().first.empty() == false)
			b->add_rk05(new rk05(files.value().first, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

		if (files.value().second.empty() == false)
			b->add_rl02(new rl02(files.value().second, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

		// TODO: allow bootloader to be selected
		if (files.value().first.empty() == false)
			setBootLoader(b, BL_RK05);
		else
			setBootLoader(b, BL_RL02);

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

	set_hostname();

	if (parts.size() == 1)
		WiFi.begin(parts.at(0).c_str());
	else
		WiFi.begin(parts.at(0).c_str(), parts.at(1).c_str());
}

void wait_network(console *const c) {
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

void check_network(console *const c) {
	wait_network(c);

	c->put_string_lf("");
	c->put_string_lf(format("Local IP address: %s", WiFi.localIP().toString().c_str()));
}

void start_network(console *const c) {
	WiFi.mode(WIFI_STA);

	set_hostname();

	WiFi.begin();

	wait_network(c);

	c->put_string_lf("");
	c->put_string_lf(format("Local IP address: %s", WiFi.localIP().toString().c_str()));
}

void set_tty_serial_speed(const int bps) {
	Serial_RS232.begin(bps);
}

void setup() {
	Serial.begin(115200);

	Serial.println(F("This PDP-11 emulator is called \"kek\" (reason for that is forgotten) and was written by Folkert van Heusden."));

	Serial.println(F("Build on: " __DATE__ " " __TIME__));

	Serial.print(F("Size of int: "));
	Serial.println(sizeof(int));

	Serial.print(F("CPU clock frequency (MHz): "));
	Serial.println(getCpuFrequencyMhz());

	Serial.print(F("Free RAM before init (decimal bytes): "));
	Serial.println(ESP.getFreeHeap());

	Serial.println(F("Init bus"));
	b = new bus();

	Serial.println(F("Init CPU"));
	c = new cpu(b, &stop_event);

	Serial.println(F("Connect CPU to BUS"));
	b->add_cpu(c);

	Serial.println(F("Init console"));
	constexpr uint32_t hwSerialConfig = SERIAL_8N1;
	Serial_RS232.begin(115200, hwSerialConfig, 16, 17);
	Serial_RS232.setHwFlowCtrlMode(0);
	const char clear_screen = 12;
	Serial_RS232.print(clear_screen);
	Serial_RS232.println(F("Console enabled on TTY"));

	std::vector<Stream *> serial_ports { &Serial_RS232, &Serial };
	cnsl = new console_esp32(&stop_event, b, serial_ports);

	Serial.println(F("Start line-frequency interrupt"));
	kw11_l *lf = new kw11_l(b, cnsl);

	running = cnsl->get_running_flag();

	Serial.println(F("Init TTY"));
	tty_ = new tty(cnsl, b);
	Serial.println(F("Connect TTY to bus"));
	b->add_tty(tty_);

	Serial.print(F("Starting panel (on CPU 0, main emulator runs on CPU "));
	Serial.print(xPortGetCoreID());
	Serial.println(F(")"));
	xTaskCreatePinnedToCore(&console_thread_wrapper_panel, "panel", 2048, cnsl, 1, nullptr, 0);

	xTaskCreatePinnedToCore(&console_thread_wrapper_io,    "c-io",  2048, cnsl, 1, nullptr, 0);

	// setup_wifi_stations();

	Serial.print(F("Free RAM after init: "));
	Serial.println(ESP.getFreeHeap());

	pinMode(LED_BUILTIN, OUTPUT);

	Serial.flush();

	cnsl->start_thread();
}

void loop() {
	debugger(cnsl, b, &stop_event, false);

	c->reset();
}
