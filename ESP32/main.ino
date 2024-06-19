// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#if defined(BUILD_FOR_RP2040)
#else
#include <WiFi.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif
#if defined(ESP32)
#include "esp_clk.h"
#include "esp_heap_caps.h"
#endif

#include "comm.h"
#include "comm_arduino.h"
#include "comm_esp32_hardwareserial.h"
#include "comm_tcp_socket_client.h"
#include "comm_tcp_socket_server.h"
#if defined(SHA2017)
#include "console_shabadge.h"
#else
#include "console_esp32.h"
#endif
#include "cpu.h"
#include "debugger.h"
#include "disk_backend.h"
#include "disk_backend_esp32.h"
#if !defined(BUILD_FOR_RP2040)
#include "disk_backend_nbd.h"
#endif
#include "error.h"
#if defined(BUILD_FOR_RP2040)
#include "rp2040.h"
#else
#include "esp32.h"
#endif
#include "gen.h"
#include "kw11-l.h"
#include "loaders.h"
#include "memory.h"
#include "tm-11.h"
#include "tty.h"
#include "utils.h"
#include "version.h"
#include "FvHNTP/FvHNTP.h"


constexpr const char SERIAL_CFG_FILE[] = "/serial.json";

bus     *b    = nullptr;
cpu     *c    = nullptr;
tty     *tty_ = nullptr;
console *cnsl = nullptr;

uint16_t exec_addr = 0;

#if !defined(BUILD_FOR_RP2040)
SdFs     SD;
#endif

std::atomic_uint32_t stop_event      { EVENT_NONE };

std::atomic_bool    *running         { nullptr    };

bool                 trace_output    { false      };

ntp                 *ntp_            { nullptr    };

comm                *cs              { nullptr    };  // Console Serial

static void console_thread_wrapper_panel(void *const c)
{
	console *const cnsl = reinterpret_cast<console *>(c);

	cnsl->panel_update_thread();

	vTaskSuspend(nullptr);
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

	uint32_t speed = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
	// sanity check
	if (speed < 300)
		speed = 115200;
	return speed;
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

#if !defined(BUILD_FOR_RP2040)
void set_hostname()
{
        uint64_t mac    = ESP.getEfuseMac();
        uint8_t *chipid = reinterpret_cast<uint8_t *>(&mac);

	char name[32];
        snprintf(name, sizeof name, "PDP11-%02x%02x%02x%02x", chipid[2], chipid[3], chipid[4], chipid[5]);

	WiFi.setHostname(name);
}

void configure_network(console *const c)
{
	WiFi.disconnect();

	WiFi.persistent(true);
	WiFi.setAutoReconnect(true);
	WiFi.useStaticBuffers(true);
	WiFi.mode(WIFI_STA);

	c->put_string_lf("Scanning for wireless networks...");
	int n_ssids = WiFi.scanNetworks();

	c->put_string_lf("Wireless networks:");
	for(int i=0; i<n_ssids; i++)
		c->put_string_lf(format("\t%s", WiFi.SSID(i).c_str()));

	c->flush_input();

	std::string wifi_ap = c->read_line("Enter SSID[|PSK]: ");
	if (wifi_ap.empty())
		return;

	auto parts = split(wifi_ap, "|");
	if (parts.size() > 2) {
		c->put_string_lf("Invalid SSID/PSK: should not contain '|'");
		return;
	}

	c->put_string_lf(format("Connecting to SSID \"%s\"", parts.at(0).c_str()));

	if (parts.size() == 1)
		WiFi.begin(parts.at(0).c_str());
	else
		WiFi.begin(parts.at(0).c_str(), parts.at(1).c_str());
}

void wait_network(console *const c)
{
	constexpr const int timeout = 10 * 3;

	int i = 0;

	while (WiFi.status() != WL_CONNECTED && i < timeout) {
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
	WiFi.mode(WIFI_STA);
	WiFi.useStaticBuffers(true);
	WiFi.begin();

	wait_network(c);

	c->put_string_lf("");
	c->put_string_lf(format("Local IP address: %s", WiFi.localIP().toString().c_str()));

	static bool dc11_loaded = false;
	if (!dc11_loaded) {
		dc11_loaded = true;

		cs->println("* Adding DC11");
		std::vector<comm *> comm_interfaces;

#if !defined(BUILD_FOR_RP2040) && defined(TTY_SERIAL_RX)
		uint32_t bitrate = load_serial_speed_configuration();

		cs->println(format("* Init TTY (on DC11), baudrate: %d bps, RX: %d, TX: %d", bitrate, TTY_SERIAL_RX, TTY_SERIAL_TX));

		comm_interfaces.push_back(new comm_esp32_hardwareserial(1, TTY_SERIAL_RX, TTY_SERIAL_TX, bitrate));
#endif

		for(size_t i=comm_interfaces.size(); i<4; i++) {
			int port = 1100 + i;
			comm_interfaces.push_back(new comm_tcp_socket_server(port));
			DOLOG(info, false, "Configuring DC11 device for TCP socket on port %d", port);
		}

		for(auto & c: comm_interfaces) {
			if (c->begin() == false)
				DOLOG(warning, false, "Failed to configure %s", c->get_identifier().c_str());
		}

		dc11 *dc11_ = new dc11(b, comm_interfaces);
		dc11_->begin();
		b->add_DC11(dc11_);

		cs->println("* Starting (NTP-) clock");
		ntp_ = new ntp("188.212.113.203");  // TODO configurable
		ntp_->begin();

		set_clock_reference(ntp_);
	}
}

void recall_configuration(console *const cnsl)
{
	cnsl->put_string_lf("Starting network...");
	start_network(cnsl);

	// TODO
}
#endif

#if defined(ESP32)
void heap_caps_alloc_failed_hook(size_t requested_size, uint32_t caps, const char *function_name)
{
	printf("%s was called but failed to allocate %d bytes with 0x%X capabilities\r\n", function_name, requested_size, caps);
}

#endif

void setup() {
	Serial.begin(115200);
	while(!Serial)
		delay(100);

	cs = new comm_arduino(&Serial, "Serial");

	cs->println("PDP11 emulator, by Folkert van Heusden");
	cs->println(format("GIT hash: %s", version_str));
	cs->println("Build on: " __DATE__ " " __TIME__);

	cs->println(format("# cores: %d, CPU frequency: %d Hz", SOC_CPU_CORES_NUM, esp_clk_cpu_freq()));

#if defined(ESP32)
	heap_caps_register_failed_alloc_callback(heap_caps_alloc_failed_hook);
#endif
#if defined(ESP32)
	set_hostname();
#endif

#if defined(BUILD_FOR_RP2040)
	SPI.setRX(MISO);
	SPI.setTX(MOSI);
	SPI.setSCK(SCK);

	for(int i=0; i<3; i++) {
		if (SD.begin(false, SD_SCK_MHZ(10), SPI))
			break;

		cs->println("Cannot initialize SD card");
	}
#endif

#if defined(BUILD_FOR_RP2040)
	LittleFSConfig cfg;
	cfg.setAutoFormat(false);

	LittleFS.setConfig(cfg);
#else
	if (!LittleFS.begin(true))
		cs->println("LittleFS.begin() failed");
#endif

	cs->println("* Init bus");
	b = new bus();

	cs->println("* Allocate memory");
	uint32_t n_pages = DEFAULT_N_PAGES;

#if !defined(BUILD_FOR_RP2040)
	cs->println(format("Free RAM after init (decimal bytes): %d", ESP.getFreeHeap()));

	if (psramInit()) {
		constexpr const uint32_t leave_unallocated = 32768;

		uint32_t free_psram = ESP.getFreePsram();
		if (free_psram > leave_unallocated) {
			n_pages = min((free_psram - leave_unallocated) / 8192, uint32_t(256));  // start size is 2 MB max (with 1 MB, UNIX 7 behaves strangely)
			cs->println(format("Free PSRAM: %d decimal bytes (or %d pages (see 'ramsize' in the debugger))", free_psram, n_pages));
		}
	}
#endif

	cs->println(format("Allocating %d (decimal) pages", n_pages));
	b->set_memory_size(n_pages);

	cs->println("* Init CPU");
	c = new cpu(b, &stop_event);
	b->add_cpu(c);

#if defined(SHA2017)
	cnsl = new console_shabadge(&stop_event, cs);
#elif defined(ESP32) || defined(BUILD_FOR_RP2040)
	cnsl = new console_esp32(&stop_event, cs, 80, 25);
#endif
	cnsl->set_bus(b);
	cnsl->begin();

	running = cnsl->get_running_flag();

	cs->println("* Connect RK05, RL02 and RP06 devices to BUS");
	auto rk05_dev = new rk05(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
	rk05_dev->begin();
	b->add_rk05(rk05_dev);

	auto rl02_dev = new rl02(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
	rl02_dev->begin();
	b->add_rl02(rl02_dev);

	auto rp06_dev = new rp06(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
	rp06_dev->begin();
	b->add_RP06(rp06_dev);

	cs->println("* Adding TTY");
	tty_ = new tty(cnsl, b);
	b->add_tty(tty_);

	cs->println("* Adding TM-11");
	b->add_tm11(new tm_11(b));

	cs->println("* Starting KW11-L");
	b->getKW11_L()->begin(cnsl);

#if !defined(SHA2017)
	pinMode(LED_BUILTIN, OUTPUT);
#endif
#if defined(HEARTBEAT_PIN)
	pinMode(HEARTBEAT_PIN, OUTPUT);
#endif

#if !defined(BUILD_FOR_RP2040) && (defined(NEOPIXELS_PIN) || defined(HEARTBEAT_PIN))
	cs->println("Starting panel");
	xTaskCreate(&console_thread_wrapper_panel, "panel", 3072, cnsl, 1, nullptr);
#endif

#if !defined(BUILD_FOR_RP2040)
	uint32_t free_heap = ESP.getFreeHeap();
	cs->println(format("Free RAM after init: %d decimal bytes", free_heap));
#endif

	cs->println("* Starting console");
	cnsl->start_thread();

	cnsl->put_string_lf("PDP-11/70 emulator, (C) Folkert van Heusden");
}

void loop()
{
	debugger(cnsl, b, &stop_event);

	c->reset();
}
