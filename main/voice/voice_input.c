/*
 * Voice Input Module
 *
 * Captures audio from an INMP441 I2S microphone, detects speech via energy VAD,
 * uploads the recording to a cloud STT API, and pushes the transcribed text
 * to the inbound message queue.
 *
 * Push-to-talk: press MIMI_VOICE_BTN_PIN to start/stop recording.
 *
 * STT provider: DashScope (Alibaba) by default; OpenAI Whisper optionally.
 *   DashScope: POST /api/v1/services/audio/asr  (JSON, file URL or base64)
 *   OpenAI   : POST /v1/audio/transcriptions     (multipart/form-data)
 */

#include "voice_input.h"
#include "voice_channel.h"
#include "mimi_config.h"
#include "bus/message_bus.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs.h"

static const char *TAG = "voice_in";

/* ---- Static state ---- */
static i2s_chan_handle_t s_rx_chan = NULL;
static bool s_initialized = false;
static volatile bool s_trigger = false;

/* Recording buffer in PSRAM */
static int16_t *s_rec_buf = NULL;

/* ---- WAV header ---- */
typedef struct __attribute__((packed)) {
    char  riff[4];        /* "RIFF"         */
    uint32_t file_size;   /* total - 8      */
    char  wave[4];        /* "WAVE"         */
    char  fmt[4];         /* "fmt "         */
    uint32_t fmt_size;    /* 16             */
    uint16_t audio_fmt;   /* 1 = PCM        */
    uint16_t channels;    /* 1              */
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits;        /* 16             */
    char  data[4];        /* "data"         */
    uint32_t data_size;
} wav_header_t;

static void fill_wav_header(wav_header_t *h, uint32_t samples)
{
    uint32_t data_bytes = samples * sizeof(int16_t);
    memcpy(h->riff, "RIFF", 4);
    h->file_size   = 36 + data_bytes;
    memcpy(h->wave, "WAVE", 4);
    memcpy(h->fmt,  "fmt ", 4);
    h->fmt_size    = 16;
    h->audio_fmt   = 1;
    h->channels    = 1;
    h->sample_rate = MIMI_VOICE_SAMPLE_RATE;
    h->byte_rate   = MIMI_VOICE_SAMPLE_RATE * sizeof(int16_t);
    h->block_align = sizeof(int16_t);
    h->bits        = 16;
    memcpy(h->data, "data", 4);
    h->data_size   = data_bytes;
}

/* ---- STT HTTP response buffer ---- */
#define STT_RESP_BUF_SIZE (4 * 1024)
static char s_stt_resp[STT_RESP_BUF_SIZE];
static int  s_stt_resp_len = 0;

static esp_err_t stt_http_evt(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int remaining = STT_RESP_BUF_SIZE - s_stt_resp_len - 1;
        int copy = evt->data_len < remaining ? evt->data_len : remaining;
        memcpy(s_stt_resp + s_stt_resp_len, evt->data, copy);
        s_stt_resp_len += copy;
        s_stt_resp[s_stt_resp_len] = '\0';
    }
    return ESP_OK;
}

/* ---- Get STT API key from NVS or config ---- */
static void get_stt_api_key(char *buf, size_t len)
{
    buf[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(MIMI_NVS_VOICE, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = len;
        nvs_get_str(h, MIMI_NVS_KEY_VOICE_KEY, buf, &sz);
        nvs_close(h);
    }
    if (!buf[0]) {
        /* Fall back to LLM API key for DashScope (same key works) */
        nvs_handle_t lh;
        if (nvs_open(MIMI_NVS_LLM, NVS_READONLY, &lh) == ESP_OK) {
            size_t sz = len;
            nvs_get_str(lh, MIMI_NVS_KEY_API_KEY, buf, &sz);
            nvs_close(lh);
        }
    }
}

/*
 * Call DashScope Paraformer STT.
 * Sends base64-encoded WAV for simplicity (avoids multipart upload).
 * Returns transcribed text in out_text (heap-allocated, caller frees).
 */
static esp_err_t call_stt_dashscope(const int16_t *pcm, uint32_t samples, char **out_text)
{
    /* Build WAV buffer in PSRAM */
    size_t wav_bytes = sizeof(wav_header_t) + samples * sizeof(int16_t);
    uint8_t *wav_buf = heap_caps_malloc(wav_bytes, MALLOC_CAP_SPIRAM);
    if (!wav_buf) return ESP_ERR_NO_MEM;

    wav_header_t hdr;
    fill_wav_header(&hdr, samples);
    memcpy(wav_buf, &hdr, sizeof(hdr));
    memcpy(wav_buf + sizeof(hdr), pcm, samples * sizeof(int16_t));

    /* Base64 encode */
    size_t b64_len = ((wav_bytes + 2) / 3) * 4 + 1;
    char *b64 = heap_caps_malloc(b64_len, MALLOC_CAP_SPIRAM);
    if (!b64) {
        free(wav_buf);
        return ESP_ERR_NO_MEM;
    }

    /* Simple base64 encode */
    static const char b64_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t j = 0;
    for (size_t i = 0; i < wav_bytes; i += 3) {
        uint8_t a = wav_buf[i];
        uint8_t b = (i + 1 < wav_bytes) ? wav_buf[i + 1] : 0;
        uint8_t c = (i + 2 < wav_bytes) ? wav_buf[i + 2] : 0;
        b64[j++] = b64_table[a >> 2];
        b64[j++] = b64_table[((a & 3) << 4) | (b >> 4)];
        b64[j++] = (i + 1 < wav_bytes) ? b64_table[((b & 0xf) << 2) | (c >> 6)] : '=';
        b64[j++] = (i + 2 < wav_bytes) ? b64_table[c & 0x3f] : '=';
    }
    b64[j] = '\0';
    free(wav_buf);

    /* Build JSON request */
    char api_key[128] = {0};
    get_stt_api_key(api_key, sizeof(api_key));
    if (!api_key[0]) {
        ESP_LOGE(TAG, "No STT API key configured");
        free(b64);
        return ESP_ERR_INVALID_STATE;
    }

    /* DashScope ASR request body */
    const char *model = MIMI_VOICE_STT_MODEL;
    size_t body_size = b64_len + 256;
    char *body = heap_caps_malloc(body_size, MALLOC_CAP_SPIRAM);
    if (!body) {
        free(b64);
        return ESP_ERR_NO_MEM;
    }

    snprintf(body, body_size,
             "{\"model\":\"%s\","
             "\"input\":{\"audio_content\":\"%s\"},"
             "\"parameters\":{\"format\":\"wav\",\"sample_rate\":%d}}",
             model, b64, MIMI_VOICE_SAMPLE_RATE);
    free(b64);

    /* HTTP POST to DashScope */
    char auth_hdr[160];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", api_key);

    s_stt_resp_len = 0;
    s_stt_resp[0] = '\0';

    esp_http_client_config_t cfg = {
        .url            = "https://dashscope.aliyuncs.com/api/v1/services/audio/asr",
        .method         = HTTP_METHOD_POST,
        .event_handler  = stt_http_evt,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms     = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_hdr);
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t ret = esp_http_client_perform(client);
    free(body);
    esp_http_client_cleanup(client);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "STT HTTP request failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Parse response: {"output":{"text":"..."}} */
    ESP_LOGD(TAG, "STT response: %.256s", s_stt_resp);
    *out_text = NULL;

    /* Simple JSON extraction (avoid cJSON overhead on large audio data path) */
    char *text_start = strstr(s_stt_resp, "\"text\":");
    if (text_start) {
        text_start += 7;
        while (*text_start == ' ' || *text_start == '"') {
            if (*text_start == '"') { text_start++; break; }
            text_start++;
        }
        char *text_end = strchr(text_start, '"');
        if (text_end) {
            size_t text_len = text_end - text_start;
            *out_text = malloc(text_len + 1);
            if (*out_text) {
                memcpy(*out_text, text_start, text_len);
                (*out_text)[text_len] = '\0';
                ESP_LOGI(TAG, "STT result: %s", *out_text);
                return ESP_OK;
            }
        }
    }

    ESP_LOGW(TAG, "Could not parse STT text from response");
    return ESP_FAIL;
}

/* ---- Recording + VAD ---- */
static uint32_t record_audio(int16_t *buf, uint32_t max_samples)
{
    const int chunk_samples = 320;   /* 20ms at 16kHz */
    const int16_t vad_threshold = 800;
    const int silence_chunks = (MIMI_VOICE_VAD_SILENCE_MS / 20);

    uint32_t total = 0;
    int silent_chunks = 0;
    size_t bytes_read = 0;

    ESP_LOGI(TAG, "Recording started (max %u ms)", MIMI_VOICE_MAX_REC_MS);

    while (total + chunk_samples <= max_samples) {
        if (voice_channel_is_speaking()) {
            /* Stop if output started playing */
            break;
        }

        esp_err_t ret = i2s_channel_read(s_rx_chan,
                                          buf + total,
                                          chunk_samples * sizeof(int16_t),
                                          &bytes_read,
                                          pdMS_TO_TICKS(100));
        if (ret != ESP_OK) break;

        /* Energy VAD */
        int32_t energy = 0;
        for (int i = 0; i < chunk_samples; i++) {
            int16_t s = buf[total + i];
            energy += (s < 0) ? -s : s;
        }
        energy /= chunk_samples;

        total += chunk_samples;

        if (energy < vad_threshold) {
            silent_chunks++;
            if (silent_chunks >= silence_chunks && total > (uint32_t)(MIMI_VOICE_SAMPLE_RATE / 2)) {
                /* >0.5s of audio and silence detected */
                ESP_LOGI(TAG, "VAD: silence detected after %u samples", total);
                break;
            }
        } else {
            silent_chunks = 0;
        }

        /* Hard time limit */
        if (total >= (uint32_t)(MIMI_VOICE_MAX_REC_MS * MIMI_VOICE_SAMPLE_RATE / 1000)) {
            ESP_LOGI(TAG, "Recording: max duration reached");
            break;
        }
    }

    ESP_LOGI(TAG, "Recording complete: %u samples (%.1f s)", total,
             (float)total / MIMI_VOICE_SAMPLE_RATE);
    return total;
}

/* ---- Voice input task ---- */
static void voice_input_task(void *arg)
{
    bool btn_was_pressed = false;

    while (1) {
        /* Check push-to-talk button */
        bool btn = (gpio_get_level(MIMI_VOICE_BTN_PIN) == 0); /* active low */

        if ((btn && !btn_was_pressed) || s_trigger) {
            s_trigger = false;
            btn_was_pressed = btn;

            if (voice_channel_is_speaking()) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            ESP_LOGI(TAG, "Starting voice capture");

            /* Record */
            uint32_t samples = record_audio(s_rec_buf, MIMI_VOICE_REC_BUF_SIZE / sizeof(int16_t));
            if (samples < (uint32_t)(MIMI_VOICE_SAMPLE_RATE / 4)) {
                ESP_LOGW(TAG, "Recording too short, discarding");
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            /* STT */
            ESP_LOGI(TAG, "Calling STT API...");
            char *text = NULL;
            esp_err_t ret = call_stt_dashscope(s_rec_buf, samples, &text);

            if (ret == ESP_OK && text && text[0]) {
                /* Push to inbound queue */
                mimi_msg_t msg = {0};
                snprintf(msg.channel, sizeof(msg.channel), "%s", MIMI_CHAN_VOICE);
                snprintf(msg.chat_id, sizeof(msg.chat_id), "voice");
                msg.content = text; /* ownership transferred to bus */

                if (message_bus_push_inbound(&msg) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to push voice message to bus");
                    free(text);
                }
            } else {
                free(text);
                ESP_LOGW(TAG, "STT failed or empty result");
            }
        } else {
            btn_was_pressed = btn;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t voice_input_init(void)
{
    /* Allocate recording buffer in PSRAM */
    s_rec_buf = heap_caps_malloc(MIMI_VOICE_REC_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_rec_buf) {
        ESP_LOGE(TAG, "Cannot allocate recording buffer (%d bytes in PSRAM)", MIMI_VOICE_REC_BUF_SIZE);
        return ESP_ERR_NO_MEM;
    }

    /* Configure I2S0 for INMP441 microphone (standard mode, rx only) */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIMI_VOICE_I2S_MIC_PORT, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S new channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIMI_VOICE_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIMI_VOICE_MIC_SCK_PIN,
            .ws   = MIMI_VOICE_MIC_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = MIMI_VOICE_MIC_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S std mode init failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return ret;
    }

    /* Configure push-to-talk button (input, active low with internal pull-up) */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << MIMI_VOICE_BTN_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    s_initialized = true;
    ESP_LOGI(TAG, "Voice input initialized: MIC I2S%d, BTN GPIO%d",
             MIMI_VOICE_I2S_MIC_PORT, MIMI_VOICE_BTN_PIN);
    return ESP_OK;
}

esp_err_t voice_input_start(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "Voice input not initialized — skipping task start");
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        voice_input_task, "voice_in",
        MIMI_VOICE_INPUT_STACK, NULL,
        MIMI_VOICE_INPUT_PRIO, NULL, 0);  /* Core 0: same as Telegram/WS */

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create voice_input task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Voice input task started");
    return ESP_OK;
}

void voice_input_trigger(void)
{
    s_trigger = true;
}
