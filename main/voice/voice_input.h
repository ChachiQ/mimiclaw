#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize the voice input subsystem.
 * Configures I2S0 for microphone capture (INMP441, 16kHz 16-bit mono)
 * and sets up the push-to-talk button GPIO.
 *
 * This function is a no-op (returns ESP_OK) if the hardware is not present
 * or I2S init fails — the error is logged but not propagated.
 *
 * @return ESP_OK always
 */
esp_err_t voice_input_init(void);

/**
 * Start the voice input FreeRTOS task.
 * Task monitors for button press / VAD trigger, records audio, calls STT,
 * and pushes text to the inbound message queue.
 *
 * @return ESP_OK if task created, ESP_FAIL otherwise
 */
esp_err_t voice_input_start(void);

/**
 * Programmatically trigger a recording session (used by voice_channel).
 * No-op if already recording or speaking.
 */
void voice_input_trigger(void);
