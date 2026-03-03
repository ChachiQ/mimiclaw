#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Initialize UART1 for peripheral communication.
 * Installs UART driver, configures GPIO pins from mimi_config.h.
 * Must be called before any send/recv operations.
 *
 * @return ESP_OK on success
 */
esp_err_t peripheral_uart_init(void);

/**
 * Send a JSON object as a newline-terminated frame.
 *
 * @param json  JSON string to send (must not contain embedded newlines)
 * @return ESP_OK on success
 */
esp_err_t peripheral_uart_send(const char *json);

/**
 * Receive one newline-terminated frame into buf.
 * Blocks until a complete line is received or timeout expires.
 *
 * @param buf         Output buffer
 * @param size        Buffer size (max bytes to read including '\0')
 * @param timeout_ms  Timeout in milliseconds
 * @return ESP_OK if a complete line was received,
 *         ESP_ERR_TIMEOUT if timeout expired,
 *         ESP_FAIL on driver error
 */
esp_err_t peripheral_uart_recv(char *buf, size_t size, int timeout_ms);

/**
 * Flush the receive buffer (discard pending bytes).
 */
void peripheral_uart_flush(void);

/**
 * Deinitialize UART1 and free driver resources.
 */
void peripheral_uart_deinit(void);
