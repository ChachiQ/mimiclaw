#pragma once

/* MimiClaw Global Configuration */

/* Build-time secrets (highest priority, override NVS) */
#if __has_include("mimi_secrets.h")
#include "mimi_secrets.h"
#endif

#ifndef MIMI_SECRET_WIFI_SSID
#define MIMI_SECRET_WIFI_SSID       ""
#endif
#ifndef MIMI_SECRET_WIFI_PASS
#define MIMI_SECRET_WIFI_PASS       ""
#endif
#ifndef MIMI_SECRET_TG_TOKEN
#define MIMI_SECRET_TG_TOKEN        ""
#endif
#ifndef MIMI_SECRET_API_KEY
#define MIMI_SECRET_API_KEY         ""
#endif
#ifndef MIMI_SECRET_MODEL
#define MIMI_SECRET_MODEL           ""
#endif
#ifndef MIMI_SECRET_MODEL_PROVIDER
#define MIMI_SECRET_MODEL_PROVIDER  "anthropic"
#endif
#ifndef MIMI_SECRET_PROXY_HOST
#define MIMI_SECRET_PROXY_HOST      ""
#endif
#ifndef MIMI_SECRET_PROXY_PORT
#define MIMI_SECRET_PROXY_PORT      ""
#endif
#ifndef MIMI_SECRET_PROXY_TYPE
#define MIMI_SECRET_PROXY_TYPE      ""
#endif
#ifndef MIMI_SECRET_SEARCH_KEY
#define MIMI_SECRET_SEARCH_KEY      ""
#endif

/* WiFi */
#define MIMI_WIFI_MAX_RETRY          10
#define MIMI_WIFI_RETRY_BASE_MS      1000
#define MIMI_WIFI_RETRY_MAX_MS       30000

/* Telegram Bot */
#define MIMI_TG_POLL_TIMEOUT_S       30
#define MIMI_TG_MAX_MSG_LEN          4096
#define MIMI_TG_POLL_STACK           (12 * 1024)
#define MIMI_TG_POLL_PRIO            5
#define MIMI_TG_POLL_CORE            0
#define MIMI_TG_CARD_SHOW_MS         3000
#define MIMI_TG_CARD_BODY_SCALE      3

/* Agent Loop */
#define MIMI_AGENT_STACK             (24 * 1024)
#define MIMI_AGENT_PRIO              6
#define MIMI_AGENT_CORE              1
#define MIMI_AGENT_MAX_HISTORY       20
#define MIMI_AGENT_MAX_TOOL_ITER     10
#define MIMI_MAX_TOOL_CALLS          4
#define MIMI_AGENT_SEND_WORKING_STATUS 1

/* Timezone (POSIX TZ format) */
#define MIMI_TIMEZONE                "PST8PDT,M3.2.0,M11.1.0"

/* LLM */
#define MIMI_LLM_DEFAULT_MODEL       "claude-opus-4-5"
#define MIMI_LLM_PROVIDER_DEFAULT    "anthropic"
#define MIMI_LLM_MAX_TOKENS          4096
#define MIMI_LLM_API_URL             "https://api.anthropic.com/v1/messages"
#define MIMI_OPENAI_API_URL          "https://api.openai.com/v1/chat/completions"
#define MIMI_LLM_API_VERSION         "2023-06-01"
#define MIMI_LLM_STREAM_BUF_SIZE     (32 * 1024)
#define MIMI_LLM_LOG_VERBOSE_PAYLOAD 0
#define MIMI_LLM_LOG_PREVIEW_BYTES   160

/* Message Bus */
#define MIMI_BUS_QUEUE_LEN           16
#define MIMI_OUTBOUND_STACK          (12 * 1024)
#define MIMI_OUTBOUND_PRIO           5
#define MIMI_OUTBOUND_CORE           0

/* Memory / SPIFFS */
#define MIMI_SPIFFS_BASE             "/spiffs"
#define MIMI_SPIFFS_CONFIG_DIR       MIMI_SPIFFS_BASE "/config"
#define MIMI_SPIFFS_MEMORY_DIR       MIMI_SPIFFS_BASE "/memory"
#define MIMI_SPIFFS_SESSION_DIR      MIMI_SPIFFS_BASE "/sessions"
#define MIMI_MEMORY_FILE             MIMI_SPIFFS_MEMORY_DIR "/MEMORY.md"
#define MIMI_SOUL_FILE               MIMI_SPIFFS_CONFIG_DIR "/SOUL.md"
#define MIMI_USER_FILE               MIMI_SPIFFS_CONFIG_DIR "/USER.md"
#define MIMI_CONTEXT_BUF_SIZE        (16 * 1024)
#define MIMI_SESSION_MAX_MSGS        20

/* Cron / Heartbeat */
#define MIMI_CRON_FILE               MIMI_SPIFFS_BASE "/cron.json"
#define MIMI_CRON_MAX_JOBS           16
#define MIMI_CRON_CHECK_INTERVAL_MS  (60 * 1000)
#define MIMI_HEARTBEAT_FILE          MIMI_SPIFFS_BASE "/HEARTBEAT.md"
#define MIMI_HEARTBEAT_INTERVAL_MS   (30 * 60 * 1000)

/* Skills */
#define MIMI_SKILLS_PREFIX           MIMI_SPIFFS_BASE "/skills/"

/* WebSocket Gateway */
#define MIMI_WS_PORT                 18789
#define MIMI_WS_MAX_CLIENTS          4

/* Serial CLI */
#define MIMI_CLI_STACK               (4 * 1024)
#define MIMI_CLI_PRIO                3
#define MIMI_CLI_CORE                0

/* NVS Namespaces */
#define MIMI_NVS_WIFI                "wifi_config"
#define MIMI_NVS_TG                  "tg_config"
#define MIMI_NVS_LLM                 "llm_config"
#define MIMI_NVS_PROXY               "proxy_config"
#define MIMI_NVS_SEARCH              "search_config"

/* NVS Keys */
#define MIMI_NVS_KEY_SSID            "ssid"
#define MIMI_NVS_KEY_PASS            "password"
#define MIMI_NVS_KEY_TG_TOKEN        "bot_token"
#define MIMI_NVS_KEY_API_KEY         "api_key"
#define MIMI_NVS_KEY_MODEL           "model"
#define MIMI_NVS_KEY_PROVIDER        "provider"
#define MIMI_NVS_KEY_PROXY_HOST      "host"
#define MIMI_NVS_KEY_PROXY_PORT      "port"

/* ================================================================
 * Peripheral Device Protocol (PDP)
 * ================================================================ */

/* UART port and GPIO pins for peripheral communication */
#define MIMI_PERIPH_UART_PORT        UART_NUM_1
#define MIMI_PERIPH_UART_BAUD        115200
#define MIMI_PERIPH_UART_TX_PIN      17
#define MIMI_PERIPH_UART_RX_PIN      18
#define MIMI_PERIPH_DETECT_PIN       21    /* DET pin: low=disconnected, high=connected */

/* UART buffer and timing */
#define MIMI_PERIPH_UART_BUF_SIZE    (4 * 1024)
#define MIMI_PERIPH_HANDSHAKE_TIMEOUT_MS  5000
#define MIMI_PERIPH_TOOL_TIMEOUT_MS       10000

/* Max peripheral tools (total MAX_TOOLS=32, minus 9 built-ins) */
#define MIMI_PERIPH_MAX_TOOLS        23

/* SPIFFS paths for peripheral state */
#define MIMI_PERIPH_DIR              MIMI_SPIFFS_BASE "/peripheral"
#define MIMI_PERIPH_MANIFEST_FILE    MIMI_PERIPH_DIR "/manifest.json"
#define MIMI_PERIPH_SKILLS_PREFIX    MIMI_SPIFFS_BASE "/skills/peripheral_"

/* ================================================================
 * Voice I/O (requires INMP441 mic + MAX98357A speaker hardware)
 * ================================================================ */

/* I2S port assignment */
#define MIMI_VOICE_I2S_MIC_PORT      I2S_NUM_0
#define MIMI_VOICE_I2S_SPK_PORT      I2S_NUM_1

/* Microphone GPIO (INMP441) */
#define MIMI_VOICE_MIC_WS_PIN        4
#define MIMI_VOICE_MIC_SCK_PIN       5
#define MIMI_VOICE_MIC_SD_PIN        6

/* Speaker GPIO (MAX98357A) */
#define MIMI_VOICE_SPK_BCLK_PIN      15
#define MIMI_VOICE_SPK_WS_PIN        16
#define MIMI_VOICE_SPK_DIN_PIN       7

/* Button pin for push-to-talk (BOOT key = GPIO0, or external) */
#define MIMI_VOICE_BTN_PIN           0

/* Recording parameters */
#define MIMI_VOICE_SAMPLE_RATE       16000
#define MIMI_VOICE_REC_BUF_SIZE      (160 * 1024)   /* 10s @ 16kHz 16-bit mono */
#define MIMI_VOICE_VAD_SILENCE_MS    1500
#define MIMI_VOICE_MAX_REC_MS        10000

/* STT / TTS provider defaults (NVS key "voice_provider" can override) */
#define MIMI_VOICE_STT_PROVIDER      "dashscope"     /* dashscope | openai */
#define MIMI_VOICE_TTS_PROVIDER      "dashscope"
#define MIMI_VOICE_STT_MODEL         "paraformer-realtime-v2"
#define MIMI_VOICE_TTS_MODEL         "cosyvoice-v1"
#define MIMI_VOICE_TTS_VOICE_ID      "longxiaochun"

/* Voice task stacks */
#define MIMI_VOICE_INPUT_STACK       (8 * 1024)
#define MIMI_VOICE_OUTPUT_STACK      (8 * 1024)
#define MIMI_VOICE_INPUT_PRIO        4
#define MIMI_VOICE_OUTPUT_PRIO       4

/* Voice channel name */
#define MIMI_CHAN_VOICE               "voice"

/* NVS namespace for voice config */
#define MIMI_NVS_VOICE               "voice_config"
#define MIMI_NVS_KEY_VOICE_PROVIDER  "provider"
#define MIMI_NVS_KEY_VOICE_KEY       "api_key"
#define MIMI_NVS_KEY_VOICE_MODEL     "model"
