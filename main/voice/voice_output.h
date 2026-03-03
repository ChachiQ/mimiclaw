#pragma once

#include "esp_err.h"

/**
 * Initialize the voice output subsystem.
 * Configures I2S1 for speaker output (MAX98357A DAC, 16kHz 16-bit mono).
 *
 * This function is a no-op (returns ESP_OK) if the hardware is not present
 * or I2S init fails — the error is logged but not propagated.
 *
 * @return ESP_OK always
 */
esp_err_t voice_output_init(void);

/**
 * Start the voice output subsystem.
 * Voice output is driven by outbound_dispatch_task calling voice_output_play().
 *
 * @return ESP_OK always
 */
esp_err_t voice_output_start(void);

/**
 * Synthesize text to speech and play it via I2S1 (blocking).
 * Called by outbound_dispatch_task when channel="voice".
 *
 * @param text  Text to synthesize
 * @return ESP_OK on success
 */
esp_err_t voice_output_play(const char *text);
