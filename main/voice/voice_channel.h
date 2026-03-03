#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Voice state machine states
 */
typedef enum {
    VOICE_STATE_IDLE = 0,
    VOICE_STATE_LISTENING,
    VOICE_STATE_PROCESSING,
    VOICE_STATE_SPEAKING,
} voice_state_t;

/**
 * Initialize the voice channel.
 * Sets up internal state; does NOT start I2S hardware.
 * Call voice_input_init() and voice_output_init() from respective modules,
 * then call this.
 *
 * @return ESP_OK always (state machine init)
 */
esp_err_t voice_channel_init(void);

/**
 * Start the voice input and output tasks.
 * Must be called after voice_channel_init().
 *
 * @return ESP_OK if tasks started successfully
 */
esp_err_t voice_channel_start(void);

/**
 * Get the current voice state.
 */
voice_state_t voice_channel_get_state(void);

/**
 * Transition to SPEAKING state (called by voice_output before TTS playback).
 * Prevents microphone capture during playback.
 */
void voice_channel_set_speaking(bool speaking);

/**
 * @return true if voice channel is currently playing audio (SPEAKING state)
 */
bool voice_channel_is_speaking(void);

/**
 * Trigger a manual recording (equivalent to pressing the button).
 * No-op if not in IDLE state.
 */
void voice_channel_trigger_listen(void);
