// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"
#if defined(ESP32)
#include <Arduino.h>
#endif
#if defined(ESP32)
#include <lwip/sockets.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <driver/uart.h>
#elif defined(_WIN32)
#include <ws2tcpip.h>
#include <winsock2.h>
#else
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#if IS_POSIX
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <thread>
#endif
#include <cstring>
#include <unistd.h>

#include "bus.h"
#include "cpu.h"
#include "dc11.h"
#include "log.h"
#include "utils.h"


#define ESP32_UART UART_NUM_1

const char *const dc11_register_names[] { "RCSR", "RBUF", "TSCR", "TBUF" };

bool setup_telnet_session(const int fd)
{
	uint8_t dont_auth[]        = { 0xff, 0xf4, 0x25 };
	uint8_t suppress_goahead[] = { 0xff, 0xfb, 0x03 };
	uint8_t dont_linemode[]    = { 0xff, 0xfe, 0x22 };
	uint8_t dont_new_env[]     = { 0xff, 0xfe, 0x27 };
	uint8_t will_echo[]        = { 0xff, 0xfb, 0x01 };
	uint8_t dont_echo[]        = { 0xff, 0xfe, 0x01 };
	uint8_t noecho[]           = { 0xff, 0xfd, 0x2d };
	// uint8_t charset[]          = { 0xff, 0xfb, 0x01 };

	if (write(fd, dont_auth, sizeof dont_auth) != sizeof dont_auth)
		return false;

	if (write(fd, suppress_goahead, sizeof suppress_goahead) != sizeof suppress_goahead)
		return false;

	if (write(fd, dont_linemode, sizeof dont_linemode) != sizeof dont_linemode)
		return false;

	if (write(fd, dont_new_env, sizeof dont_new_env) != sizeof dont_new_env)
		return false;

	if (write(fd, will_echo, sizeof will_echo) != sizeof will_echo)
		return false;

	if (write(fd, dont_echo, sizeof dont_echo) != sizeof dont_echo)
		return false;

	if (write(fd, noecho, sizeof noecho) != sizeof noecho)
		return false;

	return true;
}

dc11::dc11(const int base_port, bus *const b):
	base_port(base_port),
	b(b)
{
#if defined(_WIN32)
	pfds = new WSAPOLLFD[dc11_n_lines * 2]();
#else
	pfds = new pollfd[dc11_n_lines * 2]();
#endif

	// TODO move to begin()
	th = new std::thread(std::ref(*this));
}

dc11::~dc11()
{
	DOLOG(debug, false, "DC11 closing");

	stop_flag = true;

	if (th) {
		th->join();
		delete th;
	}

	delete [] pfds;

#if defined(ESP32)
	// won't work due to freertos thread
#elif IS_POSIX
	close(serial_fd);

	if (serial_th) {
		serial_th->join();
		delete serial_th;
	}
#endif
}

void dc11::trigger_interrupt(const int line_nr, const bool is_tx)
{
	TRACE("DC11: interrupt for line %d, %s", line_nr, is_tx ? "TX" : "RX");

	b->getCpu()->queue_interrupt(5, 0300 + line_nr * 010 + 4 * is_tx);
}

void dc11::operator()()
{
	set_thread_name("kek:DC11");

	DOLOG(info, true, "DC11 thread started");

	for(int i=0; i<dc11_n_lines; i++) {
		// client session
		pfds[dc11_n_lines + i].fd     = INVALID_SOCKET;
		pfds[dc11_n_lines + i].events = POLLIN;
#if defined(ESP32)
		if (i == 3) {  // prevent accept() on this socket
			pfds[i].fd = INVALID_SOCKET;
			continue;
		}
#endif

		// listen on port
		int port = base_port + i + 1;

		pfds[i].fd = socket(AF_INET, SOCK_STREAM, 0);

		int reuse_addr = 1;
		if (setsockopt(pfds[i].fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse_addr, sizeof(reuse_addr)) == -1) {
			close(pfds[i].fd);
			pfds[i].fd = INVALID_SOCKET;

			DOLOG(warning, true, "Cannot set reuseaddress for port %d (DC11)", port);
			continue;
		}

		set_nodelay(pfds[i].fd);

	        sockaddr_in listen_addr;
		memset(&listen_addr, 0, sizeof(listen_addr));
		listen_addr.sin_family      = AF_INET;
		listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		listen_addr.sin_port        = htons(port);

		if (bind(pfds[i].fd, reinterpret_cast<struct sockaddr *>(&listen_addr), sizeof(listen_addr)) == -1) {
			close(pfds[i].fd);
			pfds[i].fd = INVALID_SOCKET;

			DOLOG(warning, true, "Cannot bind to port %d (DC11)", port);
			continue;
		}

		if (listen(pfds[i].fd, SOMAXCONN) == -1) {
			close(pfds[i].fd);
			pfds[i].fd = INVALID_SOCKET;

			DOLOG(warning, true, "Cannot listen on port %d (DC11)", port);
			continue;
		}

		pfds[i].events = POLLIN;
	}

	while(!stop_flag) {
#if defined(_WIN32)
		int rc = WSAPoll(pfds, dc11_n_lines * 2, 100);
#else
		int rc = poll(pfds, dc11_n_lines * 2, 100);
#endif
		if (rc == 0)
			continue;

		// accept any new session
		for(int i=0; i<dc11_n_lines; i++) {
			if (pfds[i].revents != POLLIN)
				continue;

			int client_i = dc11_n_lines + i;

			// disconnect any existing client session
			// yes, one can ddos with this
			if (pfds[client_i].fd != INVALID_SOCKET) {
				close(pfds[client_i].fd);
				DOLOG(info, false, "Restarting session for port %d", base_port + i + 1);
			}

			pfds[client_i].fd = accept(pfds[i].fd, nullptr, nullptr);

			if (setup_telnet_session(pfds[client_i].fd) == false) {
				close(pfds[client_i].fd);
				pfds[client_i].fd = INVALID_SOCKET;
			}

			if (pfds[client_i].fd != INVALID_SOCKET) {
				set_nodelay(pfds[client_i].fd);

				std::unique_lock<std::mutex> lck(input_lock[i]);

				registers[i * 4 + 0] |= 0160000;  // "ERROR", RING INDICATOR, CARRIER TRANSITION
				if (is_rx_interrupt_enabled(i))
					trigger_interrupt(i, false);
			}
		}

		// receive data
		for(int i=dc11_n_lines; i<dc11_n_lines * 2; i++) {
			if (pfds[i].revents != POLLIN)
				continue;

			char buffer[32] { };
			int rc_read = read(pfds[i].fd, buffer, sizeof buffer);

			int  line_nr = i - dc11_n_lines;

			std::unique_lock<std::mutex> lck(input_lock[line_nr]);

			if (rc_read <= 0) {  // closed or error?
				DOLOG(info, false, "Failed reading from port %d", i - dc11_n_lines + 1);

				registers[line_nr * 4 + 0] |= 0140000;  // "ERROR", CARRIER TRANSITION

				close(pfds[i].fd);
				pfds[i].fd = INVALID_SOCKET;
			}
			else {
				for(int k=0; k<rc_read; k++)
					recv_buffers[line_nr].push_back(buffer[k]);

				registers[line_nr * 4 + 0] |= 128;  // DONE: bit 7
			}

			if (is_rx_interrupt_enabled(line_nr))
				trigger_interrupt(line_nr, false);
		}
	}

	DOLOG(info, true, "DC11 thread terminating");

	for(int i=0; i<dc11_n_lines * 2; i++) {
		if (pfds[i].fd != INVALID_SOCKET)
			close(pfds[i].fd);
	}
}

#if defined(ESP32)
void dc11_thread_wrapper_serial_handler(void *const c)
{
        dc11 *const d = reinterpret_cast<dc11 *>(c);

        d->serial_handler();

        vTaskSuspend(nullptr);
}

void dc11::set_serial(const int bitrate, const int rx, const int tx)
{
	if (serial_thread_running) {
		DOLOG(info, true, "DC11: serial port already configured");
		return;
	}

	Serial.printf("Tick period: %d\r\n", portTICK_PERIOD_MS);

	serial_thread_running = true;

	// Configure UART parameters
	static uart_config_t uart_config = {
		.baud_rate = bitrate,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.rx_flow_ctrl_thresh = 122,
	};
	ESP_ERROR_CHECK(uart_param_config(ESP32_UART, &uart_config));

	ESP_ERROR_CHECK(uart_set_pin(ESP32_UART, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

	// Setup UART buffered IO with event queue
	const int uart_buffer_size = 1024 * 2;
	static QueueHandle_t uart_queue;
	// Install UART driver using an event queue here
	ESP_ERROR_CHECK(uart_driver_install(ESP32_UART, uart_buffer_size, uart_buffer_size, 10, &uart_queue, 0));

	const char msg[] = "Press enter to connect\r\n";
	uart_write_bytes(ESP32_UART, msg, sizeof(msg) - 1);

	xTaskCreate(&dc11_thread_wrapper_serial_handler, "dc11_tty", 3072, this, 1, nullptr);
}
#elif IS_POSIX
void dc11::set_serial(const int bitrate, const std::string & device)
{
	serial_fd = open(device.c_str(), O_RDWR);
	if (serial_fd == -1) {
		DOLOG(warning, false, "DC11 failed to access %s: %s", device.c_str(), strerror(errno));
		return;  // TODO error handling
	}

	serial_thread_running = true;

	// from https://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c
	termios tty { };
        if (tcgetattr(serial_fd, &tty) == -1) {
		DOLOG(warning, false, "DC11 tcgetattr failed: %s", strerror(errno));
		close(serial_fd);
                return;
	}

        cfsetospeed(&tty, bitrate);
        cfsetispeed(&tty, bitrate);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
        // disable IGNBRK for mismatched speed tests; otherwise receive break
        // as \000 chars
        tty.c_iflag &= ~IGNBRK;         // disable break processing
        tty.c_lflag = 0;                // no signaling chars, no echo,
                                        // no canonical processing
        tty.c_oflag = 0;                // no remapping, no delays
        tty.c_cc[VMIN]  = 0;            // read doesn't block
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

        tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
        tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        if (tcsetattr(serial_fd, TCSANOW, &tty) == -1) {
		DOLOG(warning, false, "DC11 tcsetattr failed: %s", strerror(errno));
		close(serial_fd);
		return;
	}

	serial_th = new std::thread(&dc11::serial_handler, this);
}
#endif

void dc11::serial_handler()
{
	set_thread_name("kek:dc11-serial");

	TRACE("DC11: serial handler thread started");

#if IS_POSIX
	pollfd fds[] = { { serial_fd, POLLIN, 0 } };
#endif

	while(!stop_flag) {
		char c = 0;
#if defined(ESP32)
		yield();

		size_t n_available = 0;
		ESP_ERROR_CHECK(uart_get_buffered_data_len(ESP32_UART, &n_available));
		if (n_available == 0) {
			vTaskDelay(4 / portTICK_PERIOD_MS);
			continue;
		}

		if (uart_read_bytes(ESP32_UART, &c, 1, 100) == 0)
			continue;
#elif IS_POSIX
		int rc_poll = poll(fds, 1, 100);
		if (rc_poll == -1) {
			DOLOG(warning, false, "DC11 poll failed: %s", strerror(errno));
			break;
		}
		if (rc_poll == 0)
			continue;

		int rc_read = read(serial_fd, &c, 1);
		if (rc_read <= 0) {
			DOLOG(warning, false, "DC11 read on %d failed: %s", serial_fd, strerror(errno));
			break;
		}
#endif

		// 3 is reserved for a serial port
		constexpr const int serial_line = 3;

		std::unique_lock<std::mutex> lck(input_lock[serial_line]);

		if (serial_enabled == false) {
			TRACE("DC-11: enabling serial connection");

			serial_enabled = true;

			// first key press enables the port
			registers[serial_line * 4 + 0] |= 0160000;  // "ERROR", RING INDICATOR, CARRIER TRANSITION
		}
		else {
			TRACE("DC-11: key %d pressed", c);

			registers[serial_line * 4 + 0] |= 128;  // DONE: bit 7
		}

		recv_buffers[serial_line].push_back(c);

		if (is_rx_interrupt_enabled(serial_line))
			trigger_interrupt(serial_line, false);
	}

	TRACE("DC11: serial handler thread terminating");
}

void dc11::reset()
{
}

bool dc11::is_rx_interrupt_enabled(const int line_nr)
{
	return !!(registers[line_nr * 4 + 0] & 64);
}

bool dc11::is_tx_interrupt_enabled(const int line_nr)
{
	return !!(registers[line_nr * 4 + 2] & 64);
}

uint8_t dc11::read_byte(const uint16_t addr)
{
	uint16_t v = read_word(addr & ~1);

	if (addr & 1)
		return v >> 8;

	return v;
}

uint16_t dc11::read_word(const uint16_t addr)
{
	int      reg     = (addr - DC11_BASE) / 2;
	int      line_nr = reg / 4;
	int      sub_reg = reg & 3;

	std::unique_lock<std::mutex> lck(input_lock[line_nr]);

	uint16_t vtemp   = registers[reg];

	if (sub_reg == 0) {  // receive status
		// emulate DTR, CTS & READY
		registers[line_nr * 4 + 0] &= ~1;  // DTR: bit 0  [RCSR]
		registers[line_nr * 4 + 0] &= ~4;  // CD : bit 2

		if (pfds[line_nr + dc11_n_lines].fd != INVALID_SOCKET) {
			registers[line_nr * 4 + 0] |= 1;
			registers[line_nr * 4 + 0] |= 4;
		}

		vtemp = registers[line_nr * 4 + 0];

		// clear error(s)
		registers[line_nr * 4 + 0] &= ~0160000;
	}
	else if (sub_reg == 1) {  // read data register
		TRACE("DC11: %zu characters in buffer for line %d", recv_buffers[line_nr].size(), line_nr);

		// get oldest byte in buffer
		if (recv_buffers[line_nr].empty() == false) {
			vtemp = *recv_buffers[line_nr].begin();

			// parity check
			registers[line_nr * 4 + 0] &= ~(1 << 5);
			registers[line_nr * 4 + 0] |= parity(vtemp) << 5;

			recv_buffers[line_nr].erase(recv_buffers[line_nr].begin());

			// still data in buffer? generate interrupt
			if (recv_buffers[line_nr].empty() == false) {
				registers[line_nr * 4 + 0] |= 128;  // DONE: bit 7

				if (is_rx_interrupt_enabled(line_nr))
					trigger_interrupt(line_nr, false);
			}
		}
	}
	else if (sub_reg == 2) {  // transmit status
		registers[line_nr * 4 + 2] &= ~2;  // CTS: bit 1  [TSCR]
		registers[line_nr * 4 + 2] &= ~128;  // READY: bit 7

		if (pfds[line_nr + dc11_n_lines].fd != INVALID_SOCKET) {
			registers[line_nr * 4 + 2] |= 2;
			registers[line_nr * 4 + 2] |= 128;
		}

		vtemp = registers[line_nr * 4 + 2];
	}

	TRACE("DC11: read register %06o (\"%s\", %d line %d): %06o", addr, dc11_register_names[sub_reg], sub_reg, line_nr, vtemp);

	return vtemp;
}

void dc11::write_byte(const uint16_t addr, const uint8_t v)
{
	uint16_t vtemp = registers[(addr - DC11_BASE) / 2];
	
	if (addr & 1) {
		vtemp &= ~0xff00;
		vtemp |= v << 8;
	}
	else {
		vtemp &= ~0x00ff;
		vtemp |= v;
	}

	write_word(addr, vtemp);
}

void dc11::write_word(const uint16_t addr, const uint16_t v)
{
	int reg     = (addr - DC11_BASE) / 2;
	int line_nr = reg / 4;
	int sub_reg = reg & 3;

	std::unique_lock<std::mutex> lck(input_lock[line_nr]);

	TRACE("DC11: write register %06o (\"%s\", %d line_nr %d) to %06o", addr, dc11_register_names[sub_reg], sub_reg, line_nr, v);

	if (sub_reg == 3) {  // transmit buffer
		char c = v & 127;  // strip parity

		if (c <= 32 || c >= 127)
			TRACE("DC11: transmit [%d] on line %d", c, line_nr);
		else
			TRACE("DC11: transmit %c on line %d", c, line_nr);

		if (line_nr == 3) {
			if (serial_thread_running) {
#if defined(ESP32)
				uart_write_bytes(ESP32_UART, &c, 1);
#elif IS_POSIX
				if (write(serial_fd, &c, 1) != 1) {
					DOLOG(warning, false, "DC11 failed to send %d to (fd %d) serial port: %s", c, serial_fd, strerror(errno));
					// TODO error handling
				}
#endif
			}
			else {
				TRACE("DC11 serial line 4 not connected yet output %d", c);
			}

			if (is_tx_interrupt_enabled(line_nr))
				trigger_interrupt(line_nr, true);
			return;
		}
		SOCKET fd = pfds[dc11_n_lines + line_nr].fd;

		if (fd != INVALID_SOCKET && write(fd, &c, 1) != 1) {
			DOLOG(info, false, "DC11 line %d disconnected\n", line_nr + 1);

			registers[line_nr * 4 + 0] |= 0140000;  // "ERROR", CARRIER TRANSITION

			assert(fd != serial_fd);
			close(fd);
			pfds[dc11_n_lines + line_nr].fd = INVALID_SOCKET;
		}

		if (is_tx_interrupt_enabled(line_nr))
			trigger_interrupt(line_nr, true);
	}

	registers[reg] = v;
}
