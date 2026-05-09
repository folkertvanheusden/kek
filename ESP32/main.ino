// (C) 2018-2026 by Folkert van Heusden
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
#include <Wire.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif
#if defined(ESP32)
#include "esp_clk_tree.h"
#include "esp_heap_caps.h"
#include <SC16IS752.h>
#include "esp_pthread.h"
#endif

#include "blinkenlights.h"
#include "comm.h"
#include "comm_arduino.h"
#include "comm_esp32_hardwareserial.h"
#include "comm_esp32_SC16IS752.h"
#include "comm_tcp_socket_client.h"
#include "comm_tcp_socket_server.h"
#include "console_esp32.h"
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


bus     *b    = nullptr;
cpu     *c    = nullptr;
tty     *tty_ = nullptr;
console *cnsl = nullptr;

uint16_t exec_addr = 0;

#if !defined(BUILD_FOR_RP2040)
SdFs     SDinstance;
#endif

std::atomic_uint32_t  stop_event         { EVENT_NONE };
std::atomic_bool     *running            { nullptr    };
bool                  trace_output       { false      };
comm                 *cs                 { nullptr    };  // Console Serial
SC16IS752            *SC16IS752_a        { nullptr    };
SC16IS752            *SC16IS752_b        { nullptr    };
comm_esp32_SC16IS752 *SC16IS752_com_a[2] { nullptr    };
comm_esp32_SC16IS752 *SC16IS752_com_b[2] { nullptr    };
blinkenlights        bl;

static void console_thread_wrapper_panel(void *const c)
{
	console *const cnsl = reinterpret_cast<console *>(c);

	cnsl->panel_update_thread();

	vTaskSuspend(nullptr);
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

	static bool dz11_loaded = false;
	if (!dz11_loaded) {
		dz11_loaded = true;

    comm_io *io_channels = new comm_io(dz11_n_lines);

		cs->println("* Adding DZ11");
#if !defined(BUILD_FOR_RP2040) && defined(TTY_SERIAL_RX)
		uint32_t bitrate = get_configuration_uint32(SERIAL_CFG_FILE, 115200);

		cs->println(format("* Init TTY (on DZ11), baudrate: %d bps, RX: %d, TX: %d", bitrate, TTY_SERIAL_RX, TTY_SERIAL_TX));
    if (io_channels->set_device(0, new comm_esp32_hardwareserial(uart_port_t(1), TTY_SERIAL_RX, TTY_SERIAL_TX, bitrate)) == false)
				DOLOG(warning, false, "Failed to configure device");
#endif

		DOLOG(info, false, "Configuring TCP sockets for the remaining DZ11 slots");
		for(size_t i=0; i<dz11_n_lines; i++) {
      if (io_channels->is_defined(i))
        continue;
			int port = 1100 + i;
			DOLOG(info, false, "Configuring TCP socket on port %d for DZ11", port);
			if (io_channels->set_device(i, new comm_tcp_socket_server(port, true)) == false)
				DOLOG(warning, false, "Failed to configure device");
		}

		dz11 *dz11_ = new dz11(b, io_channels);
		dz11_->begin();
		b->add_DZ11(dz11_);

		cs->println("* Starting (NTP-) clock");
		set_clock_reference("pool.ntp.org");
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

// scan for SC16IS752 devices
bool i2c_probe(const byte addr)
{
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      cs->println(format("i2c device found at %02x", addr));
      return true;
    }
    return false;
}

void test_SC16IS752(SC16IS752 *const p, const uint8_t which)
{
  cs->println(format("PING result for SC16IS752 @ 0x%02x: %d", which, p->ping()));
}

void search_SC16IS752()
{
  cs->println("Scanning i2c bus for SC16IS752 devices...");
  Wire.begin();
  if (i2c_probe(0x4d)) {
    SC16IS752_a        = new SC16IS752(SC16IS750_PROTOCOL_I2C, 0x4d);
    SC16IS752_com_a[0] = new comm_esp32_SC16IS752(SC16IS752_a, 0, 0);
    SC16IS752_com_a[1] = new comm_esp32_SC16IS752(SC16IS752_a, 0, 1);
    test_SC16IS752(SC16IS752_a, 0x4d);
  }
  if (i2c_probe(0x4e)) {
    SC16IS752_b = new SC16IS752(SC16IS750_PROTOCOL_I2C, 0x4e);
    SC16IS752_com_b[0] = new comm_esp32_SC16IS752(SC16IS752_a, 1, 0);
    SC16IS752_com_b[1] = new comm_esp32_SC16IS752(SC16IS752_a, 1, 1);
    test_SC16IS752(SC16IS752_a, 0x4e);
  }
}

void setup() {
	Serial.begin(115200);
	while(!Serial)
		delay(100);
#if defined(ESP32)
  esp_log_level_set("*", ESP_LOG_INFO);
#endif

  heap_caps_check_integrity_all(true);

	cs = new comm_arduino(&Serial, "Serial");
	cs->println("PDP11 emulator, by Folkert van Heusden");
	cs->println(format("GIT hash: %s", version_str));
	cs->println("Build on: " __DATE__ " " __TIME__);

#if defined(ESP32)
  search_SC16IS752();
  cs->set_comm(SC16IS752_a, SC16IS752_b);
#endif

  uint32_t freq = 0;
  esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &freq);
	cs->println(format("# cores: %d, CPU frequency: %u Hz", SOC_CPU_CORES_NUM, freq));

#if defined(ESP32)
	heap_caps_register_failed_alloc_callback(heap_caps_alloc_failed_hook);

	set_hostname();
#endif

#if defined(BUILD_FOR_RP2040)
	SPI.setRX(MISO);
	SPI.setTX(MOSI);
	SPI.setSCK(SCK);

	for(int i=0; i<3; i++) {
		if (SDinstance.begin(false, SD_SCK_MHZ(10), SPI))
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
			n_pages = min((free_psram - leave_unallocated) / 8192, uint32_t(512));  // start size is 2 MB max (with 1 MB, UNIX 7 behaves strangely)
			cs->println(format("Free PSRAM: %d decimal bytes (or %d pages (see 'ramsize' in the debugger))", free_psram, n_pages));
		}

    esp_pthread_cfg_t config = esp_pthread_get_default_config();
    config.stack_alloc_caps = MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM;
    config.stack_size = 16384;
    if (auto rc = esp_pthread_set_cfg(&config); rc != ESP_OK)
      cs->println(format("esp_pthread_set_cfg(SPI_RAM) failed: %d", rc));
	}
#endif

	cs->println(format("Allocating %d (decimal) pages", n_pages));
	b->set_memory_size(n_pages);

	cs->println("* Init CPU");
	c = new cpu(b, &stop_event);
	b->add_cpu(c);

#if defined(ESP32) || defined(BUILD_FOR_RP2040)
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

	auto rp06_dev = new rp06(b, cnsl->get_disk_read_activity_flag(), cnsl->get_disk_write_activity_flag(), false);
	rp06_dev->begin();
	b->add_RP06(rp06_dev);

	cs->println("* Adding TTY");
	tty_ = new tty(cnsl, b);
	b->add_tty(tty_);

	cs->println("* Adding TM-11");
	b->add_tm11(new tm_11(b));

	cs->println("* Starting KW11-L");
	b->getKW11_L()->begin(cnsl);

	cs->println(format("LED_BUILTIN %d", LED_BUILTIN));
#if defined(HEARTBEAT_PIN)
	cs->println(format("Enable heartbeat pin on GPIO %d", HEARTBEAT_PIN));
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

  bl.begin();
  auto bl_ip = get_configuration_string(BLINKENLIGHTS_CFG_FILE, "");
  if (bl_ip.empty() == false) {
    cnsl->put_string_lf(format("Using PiDP11 blinkenlights on IP address %s", bl_ip.c_str()));
    bl.set_target(bl_ip);
  }

	cnsl->put_string_lf("PDP-11/70 emulator, (C) Folkert van Heusden");
}

void loop()
{
	debugger(cnsl, b, &stop_event, { });

	c->reset();
}
