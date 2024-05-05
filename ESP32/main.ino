// (C) 2018-2024 by Folkert van Heusden
// Released under MIT license

#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#if !defined(BUILD_FOR_RP2040)
#include <HardwareSerial.h>
#endif
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
#include "esp_heap_caps.h"
#endif

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
#include "tty.h"
#include "utils.h"
#include "version.h"


constexpr const char SERIAL_CFG_FILE[] = "/serial.json";

#if defined(BUILD_FOR_RP2040)
#define Serial_RS232 Serial1
#elif defined(CONSOLE_SERIAL_RX)
HardwareSerial       Serial_RS232(1);
#endif

bus     *b    = nullptr;
cpu     *c    = nullptr;
tty     *tty_ = nullptr;
console *cnsl = nullptr;

uint16_t exec_addr = 0;

#if !defined(BUILD_FOR_RP2040)
SdFs     SD;
#endif

std::atomic_uint32_t stop_event      { EVENT_NONE };

std::atomic_bool    *running         { nullptr };

bool                 trace_output    { false };

void console_thread_wrapper_panel(void *const c)
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
	WiFi.setHostname("PDP-11");
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
	set_hostname();

	WiFi.mode(WIFI_STA);
	WiFi.useStaticBuffers(true);
	WiFi.begin();

	wait_network(c);

	c->put_string_lf("");
	c->put_string_lf(format("Local IP address: %s", WiFi.localIP().toString().c_str()));

	static bool dc11_loaded = false;
	if (!dc11_loaded) {
		dc11_loaded = true;

		Serial.println(F("* Adding DC11"));
		dc11 *dc11_ = new dc11(1100, b);
		b->add_DC11(dc11_);
	}
}

void recall_configuration(console *const cnsl)
{
	cnsl->put_string_lf("Starting network...");
	start_network(cnsl);

	// TODO
}
#endif

#if defined(CONSOLE_SERIAL_RX)
void set_tty_serial_speed(console *const c, const uint32_t bps)
{
	Serial_RS232.begin(bps);

	if (save_serial_speed_configuration(bps) == false)
		c->put_string_lf("Failed to store configuration file with serial settings");
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

	Serial.println(F("PDP11 emulator, by Folkert van Heusden"));
	Serial.print(F("GIT hash: "));
	Serial.println(version_str);
	Serial.println(F("Build on: " __DATE__ " " __TIME__));

	Serial.print(F("Size of int: "));
	Serial.println(sizeof(int));

#if defined(ESP32)
	heap_caps_register_failed_alloc_callback(heap_caps_alloc_failed_hook);
#endif

#if !defined(BUILD_FOR_RP2040)
	Serial.print(F("CPU clock frequency (MHz): "));
	Serial.println(getCpuFrequencyMhz());
#endif

#if defined(BUILD_FOR_RP2040)
	SPI.setRX(MISO);
	SPI.setTX(MOSI);
	SPI.setSCK(SCK);

	for(int i=0; i<3; i++) {
		if (SD.begin(false, SD_SCK_MHZ(10), SPI))
			break;

		Serial.println(F("Cannot initialize SD card"));
	}
#endif

#if defined(BUILD_FOR_RP2040)
	LittleFSConfig cfg;
	cfg.setAutoFormat(false);

	LittleFS.setConfig(cfg);
#else
	if (!LittleFS.begin(true))
		Serial.println(F("LittleFS.begin() failed"));
#endif

	Serial.println(F("* Init bus"));
	b = new bus();

	Serial.println(F("* Allocate memory"));
	uint32_t n_pages = DEFAULT_N_PAGES;

#if !defined(BUILD_FOR_RP2040)
	Serial.print(F("Free RAM after init (decimal bytes): "));
	Serial.println(ESP.getFreeHeap());

	if (psramInit()) {
		uint32_t free_psram = ESP.getFreePsram();
		n_pages    = min(free_psram / 8192, uint32_t(256));  // start size is 2 MB max (with 1 MB, UNIX 7 behaves strangely)
		Serial.printf("Free PSRAM: %d decimal bytes (or %d pages (see 'ramsize' in the debugger))", free_psram, n_pages);
		Serial.println(F(""));
	}
#endif

	Serial.printf("Allocating %d (decimal) pages", n_pages);
	b->set_memory_size(n_pages);
	Serial.println(F(""));

	Serial.println(F("* Init CPU"));
	c = new cpu(b, &stop_event);
	b->add_cpu(c);

#if !defined(BUILD_FOR_RP2040) && defined(CONSOLE_SERIAL_RX)
	constexpr uint32_t hwSerialConfig = SERIAL_8N1;
	uint32_t bitrate = load_serial_speed_configuration();

	Serial.print(F("* Init console, baudrate: "));
	Serial.print(bitrate);
	Serial.println(F("bps"));

	Serial_RS232.begin(bitrate, hwSerialConfig, CONSOLE_SERIAL_RX, CONSOLE_SERIAL_TX);
	Serial_RS232.setHwFlowCtrlMode(0);

	Serial_RS232.println(F("\014Console enabled on TTY"));

	std::vector<Stream *> serial_ports { &Serial_RS232, &Serial };
#else
	std::vector<Stream *> serial_ports { &Serial };
#endif
#if defined(SHA2017)
	cnsl = new console_shabadge(&stop_event, serial_ports);
#elif defined(ESP32) || defined(BUILD_FOR_RP2040)
	cnsl = new console_esp32(&stop_event, serial_ports, 80, 25);
#endif
	cnsl->set_bus(b);
	cnsl->begin();

	running = cnsl->get_running_flag();

	Serial.println(F("* Connect RK05 and RL02 devices to BUS"));
	auto rk05_dev = new rk05(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
	rk05_dev->begin();
	b->add_rk05(rk05_dev);

	auto rl02_dev = new rl02(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag());
	rl02_dev->begin();
	b->add_rl02(rl02_dev);

	Serial.println(F("* Adding TTY"));
	tty_ = new tty(cnsl, b);
	b->add_tty(tty_);

	Serial.println(F("* Starting KW11-L"));
	b->getKW11_L()->begin(cnsl);

#if !defined(SHA2017)
	pinMode(LED_BUILTIN, OUTPUT);
#endif
#if defined(HEARTBEAT_PIN)
	pinMode(HEARTBEAT_PIN, OUTPUT);
#endif

#if !defined(BUILD_FOR_RP2040) && (defined(NEOPIXELS_PIN) || defined(HEARTBEAT_PIN))
	Serial.println(F("Starting panel"));
	xTaskCreate(&console_thread_wrapper_panel, "panel", 3072, cnsl, 1, nullptr);
#endif

#if !defined(BUILD_FOR_RP2040)
	uint32_t free_heap = ESP.getFreeHeap();
	Serial.printf("Free RAM after init: %d decimal bytes", free_heap);
	Serial.println(F(""));
#endif

	Serial.flush();

	Serial.println(F("* Starting console"));
	cnsl->start_thread();

	cnsl->put_string_lf("PDP-11/70 emulator, (C) Folkert van Heusden");
}

void loop()
{
	debugger(cnsl, b, &stop_event);

	c->reset();
}
