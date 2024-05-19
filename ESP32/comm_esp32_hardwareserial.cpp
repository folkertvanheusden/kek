// (C) 2024 by Folkert van Heusden
// Released under MIT license

#include "gen.h"

#if defined(ESP32)
#include <driver/uart.h>

#include "comm_esp32_hardwareserial.h"
#include "utils.h"


comm_esp32_hardwareserial::comm_esp32_hardwareserial(const int uart_nr, const int rx_pin, const int tx_pin, const int bitrate) :
	uart_nr(uart_nr),
	rx_pin(rx_pin), tx_pin(tx_pin),
	bitrate(bitrate)
{
}

comm_esp32_hardwareserial::~comm_esp32_hardwareserial()
{
	ESP_ERROR_CHECK(uart_driver_delete(uart_nr));
}

bool comm_esp32_hardwareserial::begin()
{
	// Configure UART parameters
	static uart_config_t uart_config = {
		.baud_rate = bitrate,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.rx_flow_ctrl_thresh = 122,
	};

	if (uart_param_config(uart_nr, &uart_config) != ESP_OK) {
		DOLOG(warning, false, "uart_param_config(%d, %d) failed", uart_nr, bitrate);
		return false;
	}

	if (uart_set_pin(uart_nr, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
		DOLOG(warning, false, "uart_set_pin(%d, %d, %d) failed", uart_nr, rx_pin, tx_pin);
		return false;
	}

	// Setup UART buffered IO with event queue
	const int uart_buffer_size = 1024 * 2;
	static QueueHandle_t uart_queue;
	// Install UART driver using an event queue here
	if (uart_driver_install(uart_nr, uart_buffer_size, uart_buffer_size, 10, &uart_queue, 0) != ESP_OK) {
		DOLOG(warning, false, "uart_driver_install(%d) failed", uart_nr);
		return false;
	}

	return true;
}

std::string comm_esp32_hardwareserial::get_identifier() const
{
	return format("UART:%d/RX:%d/TX:%d/@:%d", uart_nr, rx_pin, tx_pin, bitrate);
}

bool comm_esp32_hardwareserial::is_connected()
{
	return true;
}

bool comm_esp32_hardwareserial::has_data()
{
	size_t n_available = 0;
	ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_nr, &n_available));

	return n_available > 0;
}

uint8_t comm_esp32_hardwareserial::get_byte()
{
	while(!has_data())
		vTaskDelay(5 / portTICK_PERIOD_MS);

	uint8_t c = 0;
	uart_read_bytes(uart_nr, &c, 1, 100);  // error checking?

	return c;
}

void comm_esp32_hardwareserial::send_data(const uint8_t *const in, const size_t n)
{
	uart_write_bytes(uart_nr, in, n);  // error checking?
}
#endif
