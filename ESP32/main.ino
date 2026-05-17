// (C) 2018-2026 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#include <cstdint>
#if !defined(TEENSY4_1)
#include <LittleFS.h>
#endif
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#if defined(BUILD_FOR_PICO2W)
#include <WiFi.h>
#elif defined(TEENSY4_1)
#else
#include <Wire.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif
#if defined(ESP32)
#include <WiFi.h>
#include "esp_clk_tree.h"
#include "esp_heap_caps.h"
#include "esp_pthread.h"
#endif

#include "blinkenlights.h"
#include "comm.h"
#include "comm_arduino.h"
#if defined(ESP32)
#include "comm_esp32_hardwareserial.h"
#endif
#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
#include "comm_esp32_SC16IS752.h"
#include "comm_tcp_socket_client.h"
#include "comm_tcp_socket_server.h"
#endif
#include "console_esp32.h"
#include "cpu.h"
#include "debugger.h"
#include "disk_backend.h"
#include "disk_backend_esp32.h"
#include "disk_backend_nbd.h"
#include "error.h"
#include "kw11-l.h"
#include "loaders.h"
#include "memory.h"
#if !defined(TEENSY4_1)
#include "tm-11.h"
#endif
#include "tty.h"
#include "utils.h"
#include "version.h"


bus     *b    = nullptr;
cpu     *c    = nullptr;
tty     *tty_ = nullptr;
console *cnsl = nullptr;

uint16_t exec_addr = 0;

SdFs     SDinstance;

kek_event_t   stop_event         { EVENT_NONE };
abool        *running            { nullptr    };
bool          trace_output       { false      };
comm         *cs                 { nullptr    };  // Console Serial
#if !defined(TEENSY4_1)
blinkenlights bl;
#endif

static void console_thread_wrapper_panel(void *const c)
{
	console *const cnsl = reinterpret_cast<console *>(c);

	cnsl->panel_update_thread();

	vTaskSuspend(nullptr);
}

bool init_sd()
{
  bool disk_started = false;
#if defined(SEEED_XIAO_S3)
	cnsl->put_string_lf(format("SS  : %d", 1));
	if (SDinstance.begin(1, SD_SCK_MHZ(1)))
		disk_started = true;
#elif defined(TEENSY4_1)
  if (SD.sdfs.begin(SdSpiConfig(BUILTIN_SDCARD, SHARED_SPI, SD_SCK_MHZ(24))))
		disk_started = true;
#else
	cnsl->put_string_lf(format("SS  : %d", int(SS)));
	if (SDinstance.begin(SS, SD_SCK_MHZ(15)))
		disk_started = true;
#endif
	if (!disk_started) {
		auto err = SDinstance.sdErrorCode();
		if (err)
			DOLOG(ll_error, true, "SDerror: 0x%x, data: 0x%x", err, SDinstance.sdErrorData());
		else
			DOLOG(ll_error, true, "Failed to initialize SD card");
	}
  return disk_started;
}

const char *mac_to_string(uint8_t mac[6]) {
  static char s[20];
  sprintf(s, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return s;
}

const char *enc_to_string(uint8_t enc) {
#if defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
  switch (enc) {
    case ENC_TYPE_NONE: return "NONE";
    case ENC_TYPE_TKIP: return "WPA";
    case ENC_TYPE_CCMP: return "WPA2";
    case ENC_TYPE_AUTO: return "AUTO";
  }
#endif
  return "UNKN";
}

bool wait_network(console *const c)
{
#if !defined(TEENSY4_1)
	constexpr const int timeout = 10 * 3;
	                int i       = 0;

	while (WiFi.status() != WL_CONNECTED && i < timeout) {
		c->put_string(".");
		delay(1000 / 3);
		i++;
	}

	if (i == timeout) {
		c->put_string_lf("Time out connecting");
    return false;
  }
#endif

  return true;
}

void finish_start_network(console *const c)
{
  if (wait_network(c)) {
    c->put_string_lf("");
#if defined(TEENSY4_1)
    auto ip = qn::Ethernet.localIP();
    c->put_string_lf(format("Local IP address: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]));
#else
    c->put_string_lf(format("Local IP address: %s", WiFi.localIP().toString().c_str()));
#endif

    static bool dz11_loaded = false;
    if (!dz11_loaded) {
      dz11_loaded = true;

      comm_io *io_channels = new comm_io(dz11_n_lines);

      cs->println("* Adding DZ11");
#if !defined(BUILD_FOR_PICO2W) && defined(TTY_SERIAL_RX) && !defined(TEENSY4_1)
      uint32_t bitrate = get_configuration_uint32(SERIAL_CFG_FILE, 115200);

      cs->println(format("* Init TTY (on DZ11), baudrate: %d bps, RX: %d, TX: %d", bitrate, TTY_SERIAL_RX, TTY_SERIAL_TX));
      if (io_channels->set_device(0, new comm_esp32_hardwareserial(uart_port_t(1), TTY_SERIAL_RX, TTY_SERIAL_TX, bitrate)) == false)
        DOLOG(warning, false, "Failed to configure device");
#endif

#if !defined(BUILD_FOR_PICO2W) && !defined(TEENSY4_1)
      DOLOG(info, false, "Configuring TCP sockets for the remaining DZ11 slots");
      for(size_t i=0; i<dz11_n_lines; i++) {
        if (io_channels->is_defined(i))
          continue;
        int port = 1100 + i;
        DOLOG(info, false, "Configuring TCP socket on port %d for DZ11", port);
        if (io_channels->set_device(i, new comm_tcp_socket_server(port, true)) == false)
          DOLOG(warning, false, "Failed to configure device");
      }
#endif

      dz11 *dz11_ = new dz11(b, io_channels);
      dz11_->begin();
      b->add_DZ11(dz11_);

#if defined(ESP32)
      cs->println("* Starting (NTP-) clock");
      set_clock_reference("pool.ntp.org");
#endif
    }
  }
}

void configure_network(console *const c, const std::optional<std::string> & pars)
{
#if !defined(TEENSY4_1)
	WiFi.disconnect();

	WiFi.persistent(true);
#if defined(ESP32)
	WiFi.setAutoReconnect(true);
	WiFi.useStaticBuffers(true);
#endif
	WiFi.mode(WIFI_STA);
  delay(500);

  std::string wifi_ap;

  if (pars.has_value() == false) {
    c->put_string_lf("Scanning for wireless networks...");
    auto cnt = WiFi.scanNetworks();
    if (cnt) {
      c->put_string_lf(format("Found %d WiFi networks", cnt));
      c->put_string_lf(format("%32s %5s %17s %2s %4s", "SSID", "ENC", "BSSID        ", "CH", "RSSI"));
      for(auto i = 0; i < cnt; i++) {
        uint8_t bssid[6];
        WiFi.BSSID(i, bssid);
        c->put_string_lf(format("%32s %5s %17s %2d %4ld", WiFi.SSID(i), enc_to_string(WiFi.encryptionType(i)), mac_to_string(bssid), WiFi.channel(i), WiFi.RSSI(i)));
      }
    }
    else if (cnt < 0) {
      c->put_string_lf(format("Error during wifi scan: %d", cnt));
    }
    else {
      c->put_string_lf("No WiFi networks found");
    }

    c->flush_input();

    wifi_ap = c->read_line("Enter SSID[|PSK]: ");
    if (wifi_ap.empty())
      return;
  }
  else {
    wifi_ap = pars.value();
  }

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
#endif
  finish_start_network(c);
}

#if defined(ESP32)
void set_hostname()
{
  uint64_t mac    = ESP.getEfuseMac();
  uint8_t *chipid = reinterpret_cast<uint8_t *>(&mac);

  char name[32];
  snprintf(name, sizeof name, "PDP11-%02x%02x%02x%02x", chipid[2], chipid[3], chipid[4], chipid[5]);

  WiFi.setHostname(name);
}
#elif defined(BUILD_FOR_PICO2W)
void set_hostname()
{
  // TODO (serial number)
  WiFi.setHostname("PDP11");
}
#elif defined(TEENSY4_1)
void set_hostname()
{
  uint8_t mac[6] { };
  qn::Ethernet.macAddress(mac);

  std::string hostname = format("PDP11-%02x%02x%02x", mac[3], mac[4], mac[5]);
  qn::Ethernet.setHostname(hostname.c_str());
}
#endif

void check_network(console *const c)
{
	wait_network(c);

	c->put_string_lf("");
#if defined(TEENSY4_1)
  auto ip = qn::Ethernet.localIP();
  c->put_string_lf(format("Local IP address: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]));
#else
	c->put_string_lf(format("Local IP address: %s", WiFi.localIP().toString().c_str()));
#endif
}

void start_network(console *const c)
{
#if defined(TEENSY4_1)
  c->put_string_lf("Start Ethernet");
  if (!qn::Ethernet.begin()) {
    c->put_string_lf("Failed to start Ethernet");
    return;
  }
  c->put_string_lf("Wait for DHCP");
  if (!qn::Ethernet.waitForLocalIP(10000)) {
    c->put_string_lf("Failed to get IP address from DHCP");
    return;
  }
#else
	WiFi.mode(WIFI_STA);
#endif

  finish_start_network(c);
}

void recall_configuration(console *const cnsl)
{
	cnsl->put_string_lf("Starting network...");
	start_network(cnsl);

	// TODO
}

#if defined(ESP32)
void heap_caps_alloc_failed_hook(size_t requested_size, uint32_t caps, const char *function_name)
{
	printf("%s was called but failed to allocate %d bytes with 0x%X capabilities\r\n", function_name, requested_size, caps);
}
#endif

#if defined(TEENSY4_1)
// from https://forum.pjrc.com/index.php?threads/how-to-display-free-ram.33443/
extern char _heap_end[], *__brkval;
int freeram()
{
  return (char *)&_heap_end - __brkval;
}

void debugger_task(void *)
{
  for(;;)
    debugger(cnsl, b, &stop_event, { });
}
#endif

void stack_poller(void *)
{
  for(;;) {
    UBaseType_t   uxArraySize       = uxTaskGetNumberOfTasks();
    TaskStatus_t *pxTaskStatusArray = reinterpret_cast<TaskStatus_t *>(pvPortMalloc(uxArraySize * sizeof(TaskStatus_t)));
    if (pxTaskStatusArray != nullptr) {
      uint32_t ulTotalRunTime = 0;
      uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
      for(auto i = 0; i < uxArraySize; i++)
        printf("%d] %s %u\r\n", int(i), pxTaskStatusArray[i].pcTaskName, unsigned(pxTaskStatusArray[i].usStackHighWaterMark));
      vPortFree(pxTaskStatusArray);
    }

    vTaskDelay(1000);
  }
}

void setup() {
	Serial.begin(115200);
	while(!Serial)
		delay(100);
#if defined(TEENSY4_1)
  if (CrashReport) {
    Serial.print(CrashReport);
    CrashReport.clear();
  }
#endif
#if defined(ESP32)
  esp_log_level_set("*", ESP_LOG_INFO);
  heap_caps_check_integrity_all(true);
#endif

	cs = new comm_arduino(&Serial, "Serial");
	cs->println("PDP11 emulator, by Folkert van Heusden");
	cs->println(format("GIT hash: %s", version_str));
	cs->println("Build on: " __DATE__ " " __TIME__);
#if defined(ESP32)
	cs->println("Running on an ESP32");
#elif defined(BUILD_FOR_PICO2W)
	cs->println("Running on a Raspberry Pi Pico W");
#elif defined(TEENSY4_1)
	cs->println("Running on a Teensy 4.1");
#else
	cs->println("Unknown platform?!");
#endif

#if defined(ESP32)
	WiFi.useStaticBuffers(true);
	WiFi.begin();
#endif

#if defined(ESP32)
  search_SC16IS752(cs);

  uint32_t freq = 0;
  esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &freq);
	cs->println(format("# cores: %d, CPU frequency: %u Hz", SOC_CPU_CORES_NUM, freq));

	heap_caps_register_failed_alloc_callback(heap_caps_alloc_failed_hook);
#endif
  cs->println(format("FreeRTOS granularity: %d", portTICK_PERIOD_MS));

	set_hostname();

#if defined(BUILD_FOR_PICO2W)
	LittleFSConfig cfg;
	cfg.setAutoFormat(false);

	LittleFS.setConfig(cfg);
#elif defined(TEENSY4_1)
#else
	if (!LittleFS.begin(true))
		cs->println("LittleFS.begin() failed");
#endif

	cs->println("* Init bus");
	b = new bus();

	cs->println("* Allocate memory");
	uint32_t n_pages = DEFAULT_N_PAGES;

#if defined(BUILD_FOR_PICO2W)
	cs->println(format("Free RAM after init (decimal bytes): %d", rp2040.getFreeHeap()));
#elif defined(ESP32)
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
#elif defined(ESP32)
	cs->println(format("Free RAM after init (decimal bytes): %d", freeram()));
#endif

	cs->println("* Init CPU");
	c = new cpu(b, &stop_event);
	b->add_cpu(c);

	cs->println(format("Allocating %d (decimal) pages", n_pages));
	b->set_memory_size(n_pages);

#if defined(ESP32) || defined(BUILD_FOR_PICO2W) || defined(TEENSY4_1)
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

#if !defined(TEENSY4_1)
	cs->println("* Adding TM-11");
	b->add_tm11(new tm_11(b));
#endif

	cs->println("* Starting KW11-L");
	b->getKW11_L()->begin(cnsl);

#if defined(HEARTBEAT_PIN)
	cs->println(format("Enable heartbeat pin on GPIO %d", HEARTBEAT_PIN));
	pinMode(HEARTBEAT_PIN, OUTPUT);
#endif

#if !defined(BUILD_FOR_PICO2W) && (defined(NEOPIXELS_PIN) || defined(HEARTBEAT_PIN)) && !defined(TEENSY4_1)
	cs->println("Starting panel");
	xTaskCreate(&console_thread_wrapper_panel, "panel", 3072, cnsl, 1, nullptr);
#endif

	cs->println("* Starting console");
	cnsl->start_thread();

#if defined(BUILD_FOR_PICO2W)
	uint32_t free_heap = rp2040.getFreeHeap();
#elif defined(ESP32)
	uint32_t free_heap = ESP.getFreeHeap();
#elif defined(TEENSY4_1)
	int free_heap = freeram();
#endif
	cs->println(format("Free RAM after init: %d decimal bytes", free_heap));

#if !defined(TEENSY4_1)
  bl.begin();
  auto bl_ip = get_configuration_string(BLINKENLIGHTS_CFG_FILE, "");
  if (bl_ip.empty() == false) {
    cnsl->put_string_lf(format("Using PiDP11 blinkenlights on IP address %s", bl_ip.c_str()));
    bl.set_target(bl_ip);
  }
#endif

#if defined(TEENSY4_1)
	cnsl->put_string_lf("* Starting debugger task");
	xTaskCreate(debugger_task, "debugger", 2048, nullptr, 1, nullptr);

//  xTaskCreate(stack_poller, "stackpoller", 512, nullptr, 2, nullptr);

	cnsl->put_string_lf("* Starting task scheduler");
  vTaskStartScheduler();
#else
  cnsl->put_string_lf("PDP-11/70 emulator, (C) Folkert van Heusden");
#endif
}

void loop() {
#if !defined(TEENSY4_1)
	debugger(cnsl, b, &stop_event, { });
	c->reset();
#endif
}
