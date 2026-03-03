#pragma once

#include "esp_err.h"

/**
 * Callback type invoked when peripheral connection state changes.
 * @param connected  true = peripheral plugged in, false = unplugged
 */
typedef void (*peripheral_detect_cb_t)(bool connected);

/**
 * Initialize the peripheral detection GPIO interrupt.
 * Configures MIMI_PERIPH_DETECT_PIN with pull-down and rising/falling edge ISR.
 * Uses a FreeRTOS software timer for 50ms debounce.
 *
 * @param cb  Callback invoked on debounced state change (called from timer task)
 * @return ESP_OK on success
 */
esp_err_t peripheral_detector_init(peripheral_detect_cb_t cb);

/**
 * Read current raw state of the detect pin (no debounce).
 * @return true if peripheral is detected (pin high)
 */
bool peripheral_detector_is_connected(void);
