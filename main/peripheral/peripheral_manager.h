#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize the peripheral manager.
 * Does NOT initialize UART or detector — call those separately.
 *
 * @return ESP_OK
 */
esp_err_t peripheral_manager_init(void);

/**
 * Called by peripheral_detector when a peripheral connects.
 * Performs PDP handshake, registers dynamic tools, writes SPIFFS manifest,
 * and injects peripheral info into context_builder.
 */
void peripheral_manager_on_connect(void);

/**
 * Called by peripheral_detector when a peripheral disconnects.
 * Unregisters dynamic tools, clears context builder peripheral info.
 */
void peripheral_manager_on_disconnect(void);

/**
 * @return true if a peripheral is currently connected and ready
 */
bool peripheral_manager_is_connected(void);

/**
 * Get the connected peripheral device name (e.g. "robotic_arm").
 * Returns NULL if no peripheral is connected.
 */
const char *peripheral_manager_get_name(void);
