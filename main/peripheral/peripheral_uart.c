#include "peripheral_uart.h"
#include "mimi_config.h"

#include <string.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"

static const char *TAG = "periph_uart";

esp_err_t peripheral_uart_init(void)
{
    const uart_config_t uart_cfg = {
        .baud_rate  = MIMI_PERIPH_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(MIMI_PERIPH_UART_PORT,
                                        MIMI_PERIPH_UART_BUF_SIZE * 2,
                                        MIMI_PERIPH_UART_BUF_SIZE,
                                        0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(MIMI_PERIPH_UART_PORT, &uart_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(MIMI_PERIPH_UART_PORT,
                       MIMI_PERIPH_UART_TX_PIN,
                       MIMI_PERIPH_UART_RX_PIN,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "UART%d initialized: TX=%d RX=%d baud=%d",
             MIMI_PERIPH_UART_PORT, MIMI_PERIPH_UART_TX_PIN,
             MIMI_PERIPH_UART_RX_PIN, MIMI_PERIPH_UART_BAUD);
    return ESP_OK;
}

esp_err_t peripheral_uart_send(const char *json)
{
    if (!json) return ESP_ERR_INVALID_ARG;

    size_t len = strlen(json);
    int written = uart_write_bytes(MIMI_PERIPH_UART_PORT, json, len);
    if (written < 0) {
        ESP_LOGE(TAG, "UART write failed");
        return ESP_FAIL;
    }

    /* Send newline terminator */
    const char nl = '\n';
    uart_write_bytes(MIMI_PERIPH_UART_PORT, &nl, 1);

    ESP_LOGD(TAG, "TX: %.128s", json);
    return ESP_OK;
}

esp_err_t peripheral_uart_recv(char *buf, size_t size, int timeout_ms)
{
    if (!buf || size == 0) return ESP_ERR_INVALID_ARG;

    size_t pos = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (pos < size - 1) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            ESP_LOGW(TAG, "UART recv timeout");
            return ESP_ERR_TIMEOUT;
        }

        int remaining_ms = (int)((deadline - now) * portTICK_PERIOD_MS);
        if (remaining_ms <= 0) remaining_ms = 1;

        uint8_t ch;
        int n = uart_read_bytes(MIMI_PERIPH_UART_PORT, &ch, 1, pdMS_TO_TICKS(remaining_ms));
        if (n <= 0) continue;

        if (ch == '\n' || ch == '\r') {
            if (pos > 0) {
                buf[pos] = '\0';
                ESP_LOGD(TAG, "RX: %.128s", buf);
                return ESP_OK;
            }
            /* Skip leading newlines */
            continue;
        }

        buf[pos++] = (char)ch;
    }

    buf[pos] = '\0';
    return ESP_FAIL;  /* buffer full without newline */
}

void peripheral_uart_flush(void)
{
    uart_flush_input(MIMI_PERIPH_UART_PORT);
}

void peripheral_uart_deinit(void)
{
    uart_driver_delete(MIMI_PERIPH_UART_PORT);
    ESP_LOGI(TAG, "UART%d deinitialized", MIMI_PERIPH_UART_PORT);
}
