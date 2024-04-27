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
#include "loaders.h"
#include "memory.h"
#include "tty.h"
#include "utils.h"
#include "version.h"


constexpr const char SERIAL_CFG_FILE[] = "/serial.json";

#if defined(BUILD_FOR_RP2040)
#define Serial_RS232 Serial1
#else
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

std::vector<disk_backend *> rk05_files;
std::vector<disk_backend *> rl02_files;

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

void recall_configuration(console *const cnsl)
{
	cnsl->put_string_lf("Starting network...");
	start_network(cnsl);

	// TODO
}
#endif

void set_tty_serial_speed(console *const c, const uint32_t bps)
{
	Serial_RS232.begin(bps);

	if (save_serial_speed_configuration(bps) == false)
		c->put_string_lf("Failed to store configuration file with serial settings");
}

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

#if 1
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
#endif

#if defined(BUILD_FOR_RP2040)
	LittleFSConfig cfg;
	cfg.setAutoFormat(false);

	LittleFS.setConfig(cfg);
#else
	if (!LittleFS.begin(true))
		Serial.println(F("LittleFS.begin() failed"));
#endif

#if !defined(BUILD_FOR_RP2040)
	Serial.print(F("Free RAM after init (decimal bytes): "));
	Serial.println(ESP.getFreeHeap());

	if (psramInit()) {
		uint32_t free_psram = ESP.getFreePsram();
		Serial.printf("Free PSRAM: %d decimal bytes (or %d pages (see 'ramsize' in the debugger))", free_psram, free_psram / 8192l);
		Serial.println(F(""));
	}
#endif

	Serial.println(F("Init bus"));
	b = new bus();

	Serial.println(F("Allocate memory"));
	b->set_memory_size(DEFAULT_N_PAGES);

	Serial.println(F("Init CPU"));
	c = new cpu(b, &stop_event);

	Serial.println(F("Connect CPU to BUS"));
	b->add_cpu(c);

	constexpr uint32_t hwSerialConfig = SERIAL_8N1;
	uint32_t bitrate = load_serial_speed_configuration();

	Serial.print(F("Init console, baudrate: "));
	Serial.print(bitrate);
	Serial.println(F("bps"));

#if !defined(BUILD_FOR_RP2040)
	Serial_RS232.begin(bitrate, hwSerialConfig, 16, 17);
	Serial_RS232.setHwFlowCtrlMode(0);
#endif

	Serial_RS232.println(F("\014Console enabled on TTY"));

	std::vector<Stream *> serial_ports { &Serial_RS232, &Serial };
#if defined(SHA2017)
	cnsl = new console_shabadge(&stop_event, serial_ports);
#elif defined(ESP32) || defined(BUILD_FOR_RP2040)
	cnsl = new console_esp32(&stop_event, serial_ports, 80, 25);
#endif
	cnsl->set_bus(b);

	running = cnsl->get_running_flag();

	Serial.println(F("Connect RK05 and RL02 to BUS"));
	b->add_rk05(new rk05(rk05_files, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

	b->add_rl02(new rl02(rl02_files, b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag()));

	Serial.println(F("Init TTY"));
	tty_ = new tty(cnsl, b);
	Serial.println(F("Connect TTY to bus"));
	b->add_tty(tty_);

#if !defined(BUILD_FOR_RP2040)  // FIXME: led ring
	Serial.println(F("Starting panel"));
	xTaskCreate(&console_thread_wrapper_panel, "panel", 2048, cnsl, 1, nullptr);
#endif

#if !defined(BUILD_FOR_RP2040)
	uint32_t free_heap = ESP.getFreeHeap();
	Serial.printf("Free RAM after init: %d decimal bytes", free_heap);
	Serial.println(F(""));
#endif

#if !defined(SHA2017)
	pinMode(LED_BUILTIN, OUTPUT);
#endif

	Serial.flush();

	Serial.println(F("Starting I/O"));
	cnsl->start_thread();

	cnsl->put_string_lf("PDP-11/70 emulator, (C) Folkert van Heusden");
}

void loop()
{
	debugger(cnsl, b, &stop_event, false);

	c->reset();
}
