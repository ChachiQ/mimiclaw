#include "voice_channel.h"
#include "voice_input.h"
#include "voice_output.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "voice_chan";

static volatile voice_state_t s_state = VOICE_STATE_IDLE;

esp_err_t voice_channel_init(void)
{
    s_state = VOICE_STATE_IDLE;
    ESP_LOGI(TAG, "Voice channel initialized");
    return ESP_OK;
}

esp_err_t voice_channel_start(void)
{
    esp_err_t ret;

    ret = voice_input_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "voice_input_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = voice_output_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "voice_output_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Voice channel started");
    return ESP_OK;
}

voice_state_t voice_channel_get_state(void)
{
    return s_state;
}

void voice_channel_set_speaking(bool speaking)
{
    s_state = speaking ? VOICE_STATE_SPEAKING : VOICE_STATE_IDLE;
}

bool voice_channel_is_speaking(void)
{
    return s_state == VOICE_STATE_SPEAKING;
}

void voice_channel_trigger_listen(void)
{
    if (s_state == VOICE_STATE_IDLE) {
        s_state = VOICE_STATE_LISTENING;
        voice_input_trigger();
    }
}
