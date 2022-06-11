// (C) 2018-2022 by Folkert van Heusden
// Released under Apache License v2.0
#include <atomic>
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

void setup_wifi_stations()
{
#if 0
	WiFi.mode(WIFI_STA);

	WiFi.softAP("PDP-11 KEK", nullptr, 5, 0, 4);

#if 0
	Serial.println(F("Scanning for WiFi access points..."));

	int n = WiFi.scanNetworks();

	Serial.println(F("scan done"));

	if (n == 0)
		Serial.println(F("no networks found"));
	else {
		for (int i = 0; i < n; ++i) {
			// Print SSID and RSSI for each network found
			Serial.print(i + 1);
			Serial.print(F(": "));
			Serial.print(WiFi.SSID(i));
			Serial.print(F(" ("));
			Serial.print(WiFi.RSSI(i));
			Serial.print(F(")"));
			Serial.println(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " " : "*");
			delay(10);
		}
	}

	std::string ssid = read_terminal_line("SSID: ");
	std::string password = read_terminal_line("password: ");
	WiFi.begin(ssid.c_str(), password.c_str());
#else
	WiFi.begin("www.vanheusden.com", "Ditiseentest31415926");
	//WiFi.begin("NURDspace-guest", "harkharkhark");
#endif

	while (WiFi.status() != WL_CONNECTED) {
		Serial.print('.');

		delay(250);
	}

	on_wifi = true;

	Serial.println(WiFi.localIP());
#endif
}

// RK05, RL02 files
std::pair<std::vector<std::string>, std::vector<std::string> > select_disk_files(console *const c)
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

		std::string selected_file = c->read_line("Enter filename: ");

		c->put_string("1. RK05, 2. RL02, 3. re-select file");

		int ch = -1;
		while(ch == -1 && ch != '1' && ch != '2' && ch != '3')
			ch = c->wait_char(500);

		if (ch == '3')
			continue;

		c->put_string_lf(format("%c", ch));

		c->put_string("Opening file: ");
		c->put_string_lf(selected_file.c_str());

		File32 fh;

		if (fh.open(selected_file.c_str(), O_RDWR)) {
			fh.close();

			if (ch == '1')
				return { { selected_file }, { } };

			if (ch == '2')
				return { { }, { selected_file } };
		}

		c->put_string_lf("open failed");
	}
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

	Serial.println(F("Start line-frequency interrupt"));
	kw11_l *lf = new kw11_l(b);

	c->setEmulateMFPT(true);

	Serial.println(F("Init console"));
	cnsl = new console_esp32(&stop_event, b);

	running = cnsl->get_running_flag();

	Serial.println(F("Init TTY"));
	tty_ = new tty(cnsl);
	Serial.println(F("Connect TTY to bus"));
	b->add_tty(tty_);

	Serial.print(F("Starting panel (on CPU 0, main emulator runs on CPU "));
	Serial.print(xPortGetCoreID());
	Serial.println(F(")"));
	xTaskCreatePinnedToCore(&console_thread_wrapper_panel, "panel", 2048, cnsl, 1, nullptr, 0);

	xTaskCreatePinnedToCore(&console_thread_wrapper_io,    "c-io",  2048, cnsl, 1, nullptr, 0);

	// setup_wifi_stations();

	Serial.println(F("Load RK05"));
	auto disk_files = select_disk_files(cnsl);

	if (disk_files.first.empty() == false)
		b->add_rk05(new rk05(disk_files.first, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

	if (disk_files.second.empty() == false)
		b->add_rl02(new rl02(disk_files.second, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

	if (disk_files.first.empty() == false)
		setBootLoader(b, BL_RK05);
	else
		setBootLoader(b, BL_RL02);

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
