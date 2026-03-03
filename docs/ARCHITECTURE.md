# MimiClaw Architecture

> ESP32-S3 AI Agent firmware — C/FreeRTOS implementation running on bare metal (no Linux).

---

## System Overview

```
Telegram App (User)
    │
    │  HTTPS Long Polling
    │
    ▼
┌──────────────────────────────────────────────────┐
│               ESP32-S3 (MimiClaw)                │
│                                                  │
│   ┌─────────────┐       ┌──────────────────┐     │
│   │  Telegram    │──────▶│   Inbound Queue  │     │
│   │  Poller      │       └────────┬─────────┘     │
│   │  (Core 0)    │               │                │
│   └─────────────┘               ▼                │
│                     ┌────────────────────────┐    │
│   ┌─────────────┐  │     Agent Loop          │    │
│   │  WebSocket   │─▶│     (Core 1)           │    │
│   │  Server      │  │                        │    │
│   │  (:18789)    │  │  Context ──▶ LLM Proxy │    │
│   └─────────────┘  │  Builder      (HTTPS)   │    │
│                     │       ▲          │      │    │
│   ┌─────────────┐  │       │     tool_use?   │    │
│   │  Serial CLI  │  │       │          ▼      │    │
│   │  (Core 0)    │  │  Tool Results ◀─ Tools  │    │
│   └─────────────┘  │              (web_search)│    │
│                     └──────────┬─────────────┘    │
│                                │                  │
│                         ┌──────▼───────┐          │
│                         │ Outbound Queue│          │
│                         └──────┬───────┘          │
│                                │                  │
│                         ┌──────▼───────┐          │
│                         │  Outbound    │          │
│                         │  Dispatch    │          │
│                         │  (Core 0)    │          │
│                         └──┬────────┬──┘          │
│                            │        │             │
│                     Telegram    WebSocket          │
│                     sendMessage  send              │
│                                                   │
│   ┌──────────────────────────────────────────┐    │
│   │  SPIFFS (12 MB)                          │    │
│   │  /spiffs/config/  SOUL.md, USER.md       │    │
│   │  /spiffs/memory/  MEMORY.md, YYYY-MM-DD  │    │
│   │  /spiffs/sessions/ tg_<chat_id>.jsonl    │    │
│   └──────────────────────────────────────────┘    │
└───────────────────────────────────────────────────┘
         │
         │  Anthropic Messages API (HTTPS)
         │  + Brave Search API (HTTPS)
         ▼
   ┌───────────┐   ┌──────────────┐
   │ Claude API │   │ Brave Search │
   └───────────┘   └──────────────┘
```

---

## Three-Layer Model

MimiClaw is structured as a three-layer system. Each outer layer is optional and its initialization failure is non-fatal — the brain always continues running.

```
┌─────────────────────────────────────────────────────┐
│  Layer 3: Peripheral Capability (hot-plug, optional)│
│  GPIO21 detect → PDP v1 handshake → auto-register   │
│  tools → UART RPC → peripheral MCU executes         │
├─────────────────────────────────────────────────────┤
│  Layer 2: Voice I/O (compile flag, optional)        │
│  I2S0 INMP441 → VAD → STT → inbound_queue           │
│  outbound_queue → TTS → I2S1 MAX98357A              │
├─────────────────────────────────────────────────────┤
│  Layer 1: Brain Core (always runs independently)    │
│  Telegram + WebSocket + Agent + Tools + Memory      │
└─────────────────────────────────────────────────────┘
```

**Invariant**: Layer 1 never imports or depends on Layer 2 or Layer 3. Layer 2/3 failures are non-fatal — brain continues without them.

---

## Data Flow

```
1. User sends message on Telegram (or WebSocket)
2. Channel poller receives message, wraps in mimi_msg_t
3. Message pushed to Inbound Queue (FreeRTOS xQueue)
4. Agent Loop (Core 1) pops message:
   a. Load session history from SPIFFS (JSONL)
   b. Build system prompt (SOUL.md + USER.md + MEMORY.md + recent notes + tool guidance)
   c. Build cJSON messages array (history + current message)
   d. ReAct loop (max 10 iterations):
      i.   Call Claude API via HTTPS (non-streaming, with tools array)
      ii.  Parse JSON response → text blocks + tool_use blocks
      iii. If stop_reason == "tool_use":
           - Execute each tool (e.g. web_search → Brave Search API)
           - Append assistant content + tool_result to messages
           - Continue loop
      iv.  If stop_reason == "end_turn": break with final text
   e. Save user message + final assistant text to session file
   f. Push response to Outbound Queue
5. Outbound Dispatch (Core 0) pops response:
   a. Route by channel field:
      channel=="telegram"  → telegram_send_message()
      channel=="websocket" → ws_server_send()
      channel=="voice"     → voice_output_play()
6. User receives reply
```

---

## Module Map

```
main/
├── mimi.c                  Entry point — app_main() orchestrates init + startup
├── mimi_config.h           All compile-time constants + build-time secrets include
├── mimi_secrets.h          Build-time credentials (gitignored, highest priority)
├── mimi_secrets.h.example  Template for mimi_secrets.h
│
├── bus/
│   ├── message_bus.h       mimi_msg_t struct, queue API
│   └── message_bus.c       Two FreeRTOS queues: inbound + outbound
│
├── wifi/
│   ├── wifi_manager.h      WiFi STA lifecycle API
│   └── wifi_manager.c      Event handler, exponential backoff
│
├── telegram/
│   ├── telegram_bot.h      Bot init/start, send_message API
│   └── telegram_bot.c      Long polling loop, JSON parsing, message splitting
│
├── llm/
│   ├── llm_proxy.h         llm_chat() + llm_chat_tools() API, tool_use types
│   └── llm_proxy.c         Anthropic/OpenAI API (non-streaming), tool_use parsing
│
├── agent/
│   ├── agent_loop.h        Agent task init/start
│   ├── agent_loop.c        ReAct loop: LLM call → tool execution → repeat
│   ├── context_builder.h   System prompt + messages builder API
│   └── context_builder.c   Reads bootstrap files + memory + skill summaries + tool guidance
│
├── tools/
│   ├── tool_registry.h     Tool definition struct, register/dispatch API
│   ├── tool_registry.c     Tool registration, JSON schema builder, dispatch by name
│   ├── tool_web_search.h/c Brave Search API via HTTPS (direct + proxy)
│   ├── tool_get_time.h/c   NTP-synchronized current time
│   ├── tool_files.h/c      SPIFFS file I/O (read_file, write_file, edit_file, list_dir)
│   ├── tool_cron.h/c       Cron job management (cron_add, cron_list, cron_remove)
│   └── tool_peripheral.h/c Universal UART RPC stub for all peripheral tools
│
├── memory/
│   ├── memory_store.h      Long-term + daily memory API
│   ├── memory_store.c      MEMORY.md read/write, daily .md append/read
│   ├── session_mgr.h       Per-chat session API
│   └── session_mgr.c       JSONL session files, ring buffer history
│
├── gateway/
│   ├── ws_server.h         WebSocket server API
│   └── ws_server.c         ESP HTTP server with WS upgrade, client tracking
│
├── proxy/
│   ├── http_proxy.h        Proxy connection API
│   └── http_proxy.c        HTTP CONNECT tunnel + TLS via esp_tls
│
├── cli/
│   ├── serial_cli.h        CLI init API
│   └── serial_cli.c        esp_console REPL with config + debug + maintenance commands
│
├── cron/
│   ├── cron_service.h      Cron scheduler API (init/start/add/remove/list)
│   └── cron_service.c      Persistent job store (cron.json), FreeRTOS task, inbound inject
│
├── heartbeat/
│   ├── heartbeat.h         Heartbeat API (init/start/trigger)
│   └── heartbeat.c         FreeRTOS timer (30 min), reads HEARTBEAT.md, triggers agent
│
├── skills/
│   ├── skill_loader.h      Skill loader API (init/build_summary)
│   └── skill_loader.c      Loads .md files from /spiffs/skills/, injects into system prompt
│
├── peripheral/
│   ├── peripheral_uart.h/c     UART1 driver (GPIO17=TX, GPIO18=RX, 115200 baud, 8N1)
│   ├── peripheral_detector.h/c GPIO21 hot-plug detection (50ms debounce, edge ISR)
│   ├── peripheral_protocol.h/c PDP v1: handshake, manifest parse, skill xfer, tool RPC
│   └── peripheral_manager.h/c  Lifecycle: on_connect→handshake+register, on_disconnect→unregister
│
├── voice/
│   ├── voice_input.h/c         I2S0 INMP441 (GPIO4=BCK/GPIO5=WS/GPIO6=DATA_IN), energy VAD, PTT GPIO0, DashScope STT
│   ├── voice_output.h/c        I2S1 MAX98357A (GPIO15=BCK/GPIO16=WS/GPIO7=DATA_OUT), DashScope TTS, PCM playback
│   └── voice_channel.h/c       State machine: IDLE/LISTENING/PROCESSING/SPEAKING
│
└── ota/
    ├── ota_manager.h       OTA update API
    └── ota_manager.c       esp_https_ota wrapper
```

---

## FreeRTOS Task Layout

| Task               | Core | Priority | Stack  | Description                          |
|--------------------|------|----------|--------|--------------------------------------|
| `tg_poll`          | 0    | 5        | 12 KB  | Telegram long polling (30s timeout)  |
| `agent_loop`       | 1    | 6        | 24 KB  | Message processing + LLM API call    |
| `outbound`         | 0    | 5        | 12 KB  | Route responses to Telegram / WS     |
| `cron`             | any  | 4        | 4 KB   | Polls due cron jobs every 60s        |
| `serial_cli`       | 0    | 3        | 4 KB   | USB serial console REPL              |
| `voice_input`      | 1    | 5        | 8 KB   | VAD + STT pipeline                   |
| `voice_output`     | 0    | 5        | 8 KB   | TTS + I2S playback                   |
| httpd (internal)   | 0    | 5        | —      | WebSocket server (esp_http_server)   |
| wifi_event (IDF)   | 0    | 8        | —      | WiFi event handling (ESP-IDF)        |
| heartbeat (timer)  | —    | —        | —      | FreeRTOS timer, fires every 30 min   |

**Core allocation strategy**: Core 0 handles I/O (network, serial, WiFi). Core 1 is dedicated to the agent loop (CPU-bound JSON building + waiting on HTTPS).

---

## Memory Budget

| Purpose                            | Location       | Size     |
|------------------------------------|----------------|----------|
| FreeRTOS task stacks               | Internal SRAM  | ~40 KB   |
| WiFi buffers                       | Internal SRAM  | ~30 KB   |
| TLS connections x2 (Telegram + Claude) | PSRAM      | ~120 KB  |
| JSON parse buffers                 | PSRAM          | ~32 KB   |
| Session history cache              | PSRAM          | ~32 KB   |
| System prompt buffer               | PSRAM          | ~16 KB   |
| LLM response stream buffer         | PSRAM          | ~32 KB   |
| Remaining available                | PSRAM          | ~7.7 MB  |

Large buffers (32 KB+) are allocated from PSRAM via `heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM)`.

---

## Flash Partition Layout

```
Offset      Size      Name        Purpose
─────────────────────────────────────────────
0x009000    24 KB     nvs         ESP-IDF internal use (WiFi calibration etc.)
0x00F000     8 KB     otadata     OTA boot state
0x011000     4 KB     phy_init    WiFi PHY calibration
0x020000     2 MB     ota_0       Firmware slot A
0x220000     2 MB     ota_1       Firmware slot B
0x420000    12 MB     spiffs      Markdown memory, sessions, config
0xFF0000    64 KB     coredump    Crash dump storage
```

Total: 16 MB flash.

---

## Storage Layout (SPIFFS)

SPIFFS is a flat filesystem — no real directories. Files use path-like names.

```
/spiffs/config/SOUL.md              AI personality definition
/spiffs/config/USER.md              User profile
/spiffs/memory/MEMORY.md            Long-term persistent memory
/spiffs/memory/2026-02-05.md        Daily notes (one file per day)
/spiffs/sessions/tg_12345.jsonl     Session history (one file per Telegram chat)
/spiffs/cron.json                   Persisted cron job definitions
/spiffs/HEARTBEAT.md                Pending tasks checked every 30 minutes
/spiffs/skills/weather.md           Built-in skill (loaded on first boot)
/spiffs/skills/daily_briefing.md    Built-in skill
/spiffs/skills/skill_creator.md     Built-in skill
/spiffs/skills/<custom>.md          Custom skills added by the agent or user
/spiffs/peripheral/manifest.json    Last received peripheral manifest
/spiffs/skills/peripheral_<name>.md Peripheral skill files (auto-transferred via PDP v1)
```

Session files are JSONL (one JSON object per line):
```json
{"role":"user","content":"Hello","ts":1738764800}
{"role":"assistant","content":"Hi there!","ts":1738764802}
```

---

## Configuration

MimiClaw uses a **two-layer configuration system**:

1. **Build-time secrets** (`mimi_secrets.h`): highest priority, baked into firmware
2. **Runtime NVS overrides**: set via CLI commands (`set_wifi`, `set_api_key`, etc.), survive reboots

At runtime, each subsystem reads NVS first; if no NVS key is set, it falls back to the build-time value. Use `config_show` to inspect effective values and `config_reset` to wipe all NVS overrides back to build-time defaults.

**Build-time defines** (`mimi_secrets.h`):

| Define                          | Description                             |
|---------------------------------|-----------------------------------------|
| `MIMI_SECRET_WIFI_SSID`        | WiFi SSID                               |
| `MIMI_SECRET_WIFI_PASS`        | WiFi password                           |
| `MIMI_SECRET_TG_TOKEN`         | Telegram Bot API token                  |
| `MIMI_SECRET_API_KEY`          | LLM API key (Anthropic or OpenAI)       |
| `MIMI_SECRET_MODEL`            | Model ID (default: claude-opus-4-5)     |
| `MIMI_SECRET_MODEL_PROVIDER`   | `anthropic` or `openai` (default: anthropic) |
| `MIMI_SECRET_PROXY_HOST`       | HTTP/SOCKS5 proxy hostname/IP (optional)|
| `MIMI_SECRET_PROXY_PORT`       | Proxy port (optional)                   |
| `MIMI_SECRET_PROXY_TYPE`       | `http` or `socks5` (optional)           |
| `MIMI_SECRET_SEARCH_KEY`       | Brave Search API key (optional)         |

**NVS namespaces** (mirroring the build-time defines):
`wifi_config`, `tg_config`, `llm_config`, `proxy_config`, `search_config`

---

## Message Bus Protocol

The internal message bus uses two FreeRTOS queues carrying `mimi_msg_t`:

```c
typedef struct {
    char channel[16];   // "telegram", "websocket", "cli", "voice"
    char chat_id[32];   // Telegram chat ID or WS client ID
    char *content;      // Heap-allocated text (ownership transferred)
} mimi_msg_t;
```

- **Inbound queue**: channels → agent loop (depth: 8)
- **Outbound queue**: agent loop → dispatch → channels (depth: 8)
- Content string ownership is transferred on push; receiver must `free()`.

---

## WebSocket Protocol

Port: **18789**. Max clients: **4**.

**Client → Server:**
```json
{"type": "message", "content": "Hello", "chat_id": "ws_client1"}
```

**Server → Client:**
```json
{"type": "response", "content": "Hi there!", "chat_id": "ws_client1"}
```

Client `chat_id` is auto-assigned on connection (`ws_<fd>`) but can be overridden in the first message.

---

## Claude API Integration

Endpoint: `POST https://api.anthropic.com/v1/messages`

Request format (Anthropic-native, non-streaming, with tools):
```json
{
  "model": "claude-opus-4-6",
  "max_tokens": 4096,
  "system": "<system prompt>",
  "tools": [
    {
      "name": "web_search",
      "description": "Search the web for current information.",
      "input_schema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}
    }
  ],
  "messages": [
    {"role": "user", "content": "Hello"},
    {"role": "assistant", "content": "Hi!"},
    {"role": "user", "content": "What's the weather today?"}
  ]
}
```

Key difference from OpenAI: `system` is a top-level field, not inside the `messages` array.

Non-streaming JSON response:
```json
{
  "id": "msg_xxx",
  "type": "message",
  "role": "assistant",
  "content": [
    {"type": "text", "text": "Let me search for that."},
    {"type": "tool_use", "id": "toulu_xxx", "name": "web_search", "input": {"query": "weather today"}}
  ],
  "stop_reason": "tool_use"
}
```

When `stop_reason` is `"tool_use"`, the agent loop executes each tool and sends results back:
```json
{"role": "assistant", "content": [<text + tool_use blocks>]}
{"role": "user", "content": [{"type": "tool_result", "tool_use_id": "toolu_xxx", "content": "..."}]}
```

The loop repeats until `stop_reason` is `"end_turn"` (max 10 iterations).

---

## Tool Registry

Nine built-in tools are registered at startup in `tools/tool_registry.c`:

| Tool               | Source file          | Description                                            |
|--------------------|----------------------|--------------------------------------------------------|
| `web_search`       | `tool_web_search.c`  | Brave Search API (direct or via proxy)                 |
| `get_current_time` | `tool_get_time.c`    | NTP-synced current date + time, sets system clock      |
| `read_file`        | `tool_files.c`       | Read a file from SPIFFS (`/spiffs/...`)                |
| `write_file`       | `tool_files.c`       | Write or overwrite a file on SPIFFS                    |
| `edit_file`        | `tool_files.c`       | Find-and-replace first occurrence in a SPIFFS file     |
| `list_dir`         | `tool_files.c`       | List SPIFFS files (optional path prefix filter)        |
| `cron_add`         | `tool_cron.c`        | Schedule a recurring (`every`) or one-shot (`at`) task |
| `cron_list`        | `tool_cron.c`        | List all scheduled cron jobs                           |
| `cron_remove`      | `tool_cron.c`        | Remove a cron job by ID                                |

Each tool is defined as a `mimi_tool_t` struct with a name, description, JSON Schema for inputs, and an `execute` function pointer. Up to 4 tool calls are executed in parallel per ReAct iteration.

Peripheral tools are registered dynamically at runtime via `tool_registry_register_dynamic()` when a peripheral connects, and unregistered via `unregister_peripheral_tools()` on disconnect. After each registration change, `rebuild_json()` regenerates the tools JSON array sent to the LLM.

---

## Peripheral Subsystem

The peripheral subsystem (`main/peripheral/`) implements PDP v1 (Peripheral Declaration Protocol), enabling the ESP32-S3 brain to hot-plug external MCUs that extend its tool capabilities over UART.

### Files

| File                      | Purpose                                                              |
|---------------------------|----------------------------------------------------------------------|
| `peripheral_uart.c/h`     | UART1 driver: GPIO17=TX, GPIO18=RX, 115200 baud, 8N1, ring buffer RX |
| `peripheral_detector.c/h` | GPIO21 hot-plug detection: 50ms debounce, edge ISR via `gpio_isr_handler_add` |
| `peripheral_protocol.c/h` | PDP v1: handshake, manifest parse, skill transfer, tool RPC over UART |
| `peripheral_manager.c/h`  | Lifecycle manager: `on_connect` → handshake+register; `on_disconnect` → unregister |

### PDP v1 Protocol Flow

```
1. GPIO21 goes HIGH
   └── peripheral_detector ISR fires → peripheral_manager.on_connect()

2. Handshake
   Brain sends:      {"type":"hello","version":"1"}
   Peripheral acks:  {"type":"hello_ack","version":"1"}

3. Manifest exchange
   Brain sends:      {"type":"manifest_req"}
   Peripheral sends: manifest JSON (tool list with names, descriptions, schemas)
   Brain saves to:   /spiffs/peripheral/manifest.json

4. Skill transfer (for each tool in manifest)
   Brain requests:   {"type":"skill_req","name":"<tool_name>"}
   Peripheral sends: skill .md content
   Brain saves to:   /spiffs/skills/peripheral_<name>.md

5. Tool registration
   tool_registry_register_dynamic() registers each peripheral tool
   as a UART RPC stub (tool_peripheral.c execute function)

6. GPIO21 goes LOW
   └── peripheral_manager.on_disconnect()
       ├── unregister_peripheral_tools()   remove all peripheral tools
       └── rebuild_json()                  rebuild tools JSON array
```

### Key APIs

```c
// Dynamic tool registration (tool_registry.c)
esp_err_t tool_registry_register_dynamic(const char *name,
                                         const char *description,
                                         const char *schema_json,
                                         tool_execute_fn execute_fn);

// Remove all peripheral tools and rebuild JSON
void unregister_peripheral_tools(void);
void rebuild_json(void);
```

All peripheral tools share a single execute function in `tool_peripheral.c`. When the agent invokes a peripheral tool, the stub serializes the call as a JSON RPC message over UART1 and blocks waiting for the peripheral's response.

---

## Voice Subsystem

The voice subsystem (`main/voice/`) adds hands-free interaction via a microphone (INMP441) and speaker amplifier (MAX98357A). It integrates with DashScope for cloud STT and TTS. Both `voice_input_init()` and `voice_output_init()` failures are **non-fatal** — they log a warning and the brain continues operating without voice.

### Files

| File                  | Purpose                                                                          |
|-----------------------|----------------------------------------------------------------------------------|
| `voice_input.c/h`     | I2S0 INMP441 (GPIO4=BCK, GPIO5=WS, GPIO6=DATA_IN), energy-based VAD, PTT GPIO0, DashScope STT |
| `voice_output.c/h`    | I2S1 MAX98357A (GPIO15=BCK, GPIO16=WS, GPIO7=DATA_OUT), DashScope CosyVoice TTS, PCM playback |
| `voice_channel.c/h`   | State machine: IDLE → LISTENING → PROCESSING → SPEAKING → IDLE                  |

### Voice Flow

```
1. VAD detects speech energy above threshold (or PTT GPIO0 pressed)
   └── voice_channel transitions IDLE → LISTENING

2. Audio buffer fills until silence detected (or PTT released)
   └── voice_channel transitions LISTENING → PROCESSING

3. STT: DashScope API call with PCM audio → transcribed text
   └── text injected into inbound_queue as mimi_msg_t { channel="voice" }

4. Agent processes message normally (ReAct loop, tools, etc.)
   └── response pushed to outbound_queue with channel="voice"

5. TTS: outbound_dispatch routes channel=="voice" → voice_output_play()
   └── DashScope CosyVoice API call → PCM audio stream
   └── voice_channel transitions PROCESSING → SPEAKING
   └── PCM audio written to I2S1 → MAX98357A amplifier → speaker

6. Playback complete
   └── voice_channel transitions SPEAKING → IDLE
```

### Hardware Pin Assignment

| Signal        | GPIO | Interface |
|---------------|------|-----------|
| I2S0 BCK      | 4    | INMP441 microphone clock |
| I2S0 WS       | 5    | INMP441 word select      |
| I2S0 DATA_IN  | 6    | INMP441 data             |
| PTT button    | 0    | Push-to-talk (active low)|
| I2S1 BCK      | 15   | MAX98357A amplifier clock|
| I2S1 WS       | 16   | MAX98357A word select    |
| I2S1 DATA_OUT | 7    | MAX98357A data           |

---

## Startup Sequence

```
app_main()
  │
  ├── Phase 1 — Core Init (always runs):
  │   ├── init_nvs()                    NVS flash init (erase if corrupted)
  │   ├── esp_event_loop_create_default()
  │   ├── init_spiffs()                 Mount SPIFFS at /spiffs
  │   ├── message_bus_init()            Create inbound + outbound queues
  │   ├── memory_store_init()           Verify SPIFFS paths
  │   ├── skill_loader_init()           Load built-in skills to SPIFFS, scan /spiffs/skills/
  │   ├── session_mgr_init()
  │   ├── wifi_manager_init()           Init WiFi STA mode + event handlers
  │   ├── http_proxy_init()             Load proxy config (NVS → build-time fallback)
  │   ├── telegram_bot_init()           Load bot token (NVS → build-time fallback)
  │   ├── llm_proxy_init()              Load API key + model (NVS → build-time fallback)
  │   ├── tool_registry_init()          Register 9 built-in tools, build tools JSON
  │   ├── cron_service_init()           Load persisted cron jobs from /spiffs/cron.json
  │   ├── heartbeat_init()              Create FreeRTOS timer (30 min interval)
  │   ├── agent_loop_init()
  │   └── serial_cli_init()             Start REPL (works without WiFi)
  │
  ├── Phase 2 — Peripheral Init (non-fatal):
  │   ├── peripheral_manager_init()     Init peripheral subsystem state
  │   ├── peripheral_detector_init()    Configure GPIO21 edge ISR (50ms debounce)
  │   └── peripheral_uart_init()        Init UART1 (GPIO17=TX, GPIO18=RX, 115200 baud)
  │
  ├── Phase 3 — Voice Init (non-fatal):
  │   ├── voice_input_init()            Init I2S0 + VAD (log warning if fails, brain continues)
  │   └── voice_output_init()           Init I2S1 + TTS (log warning if fails, brain continues)
  │
  ├── wifi_manager_start()              Connect using NVS credentials (fallback to build-time)
  │   └── wifi_manager_wait_connected(30s)
  │
  └── [if WiFi connected]
      ├── outbound_dispatch task        Launch outbound task (Core 0) — first to avoid drops
      ├── agent_loop_start()            Launch agent_loop task (Core 1)
      ├── telegram_bot_start()          Launch tg_poll task (Core 0)
      ├── cron_service_start()          Launch cron task (any core, prio 4)
      ├── heartbeat_start()             Start 30-min FreeRTOS timer
      └── ws_server_start()             Start httpd on port 18789
```

If WiFi credentials are missing or connection times out, the CLI remains available for diagnostics.

---

## Serial CLI Commands

The CLI provides both runtime configuration and debug/maintenance commands. Configuration set via CLI is saved to NVS and survives reboots; it overrides build-time values until `config_reset` is run.

**Configuration commands** (saved to NVS):

| Command                              | Description                                         |
|--------------------------------------|-----------------------------------------------------|
| `set_wifi <ssid> <password>`         | Set WiFi SSID and password                          |
| `set_tg_token <token>`              | Set Telegram bot token                              |
| `set_api_key <key>`                 | Set LLM API key (Anthropic or OpenAI)               |
| `set_model <model>`                 | Set LLM model identifier                            |
| `set_model_provider <provider>`     | Set provider: `anthropic` or `openai`               |
| `set_proxy <host> <port> [type]`    | Set HTTP/SOCKS5 proxy (type: `http` or `socks5`)    |
| `clear_proxy`                       | Remove proxy configuration                          |
| `set_search_key <key>`              | Set Brave Search API key                            |
| `config_show`                       | Show current config (source: NVS or build)          |
| `config_reset`                      | Wipe all NVS overrides, revert to build-time values |

**Debug and maintenance commands**:

| Command                        | Description                                      |
|--------------------------------|--------------------------------------------------|
| `wifi_status`                  | Show connection status and IP                    |
| `wifi_scan`                    | Scan and list nearby WiFi APs                    |
| `memory_read`                  | Print MEMORY.md contents                         |
| `memory_write <content>`       | Overwrite MEMORY.md                              |
| `session_list`                 | List all session files                           |
| `session_clear <chat_id>`      | Delete a session file                            |
| `heap_info`                    | Show internal + PSRAM free bytes                 |
| `skill_list`                   | List installed skills                            |
| `skill_show <name>`            | Print full content of a skill file               |
| `skill_search <keyword>`       | Search skill files by keyword                    |
| `heartbeat_trigger`            | Manually trigger a heartbeat check               |
| `cron_start`                   | Start the cron scheduler task                    |
| `tool_exec <name> [json]`      | Execute a registered tool directly               |
| `restart`                      | Reboot the device                                |
| `help`                         | List all available commands                      |

---

## Nanobot Reference Mapping

| Nanobot Module              | MimiClaw Equivalent            | Notes                        |
|-----------------------------|--------------------------------|------------------------------|
| `agent/loop.py`             | `agent/agent_loop.c`           | ReAct loop with tool use     |
| `agent/context.py`          | `agent/context_builder.c`      | Loads SOUL.md + USER.md + memory + skill summaries + tool guidance |
| `agent/memory.py`           | `memory/memory_store.c`        | MEMORY.md + daily notes      |
| `session/manager.py`        | `memory/session_mgr.c`         | JSONL per chat, ring buffer  |
| `channels/telegram.py`      | `telegram/telegram_bot.c`      | Raw HTTP, no python-telegram-bot |
| `bus/events.py` + `queue.py`| `bus/message_bus.c`            | FreeRTOS queues vs asyncio   |
| `providers/litellm_provider.py` | `llm/llm_proxy.c`         | Anthropic + OpenAI-compatible API |
| `config/schema.py`          | `mimi_config.h` + `mimi_secrets.h` | Build-time + NVS runtime override |
| `cli/commands.py`           | `cli/serial_cli.c`             | esp_console REPL, full config + debug commands |
| `agent/tools/*`             | `tools/tool_registry.c` + `tool_*.c` | 9 built-in tools (web_search, time, file I/O, cron) |
| `agent/subagent.py`         | *(not yet implemented)*        | See TODO.md                  |
| `agent/skills.py`           | `skills/skill_loader.c`        | .md files from /spiffs/skills/, injected into system prompt |
| `cron/service.py`           | `cron/cron_service.c`          | FreeRTOS task, every/at jobs, persisted to cron.json |
| `heartbeat/service.py`      | `heartbeat/heartbeat.c`        | FreeRTOS timer, 30-min interval, reads HEARTBEAT.md |
| *(no equivalent)*           | `peripheral/`                  | MimiClaw-original: PDP v1 hot-plug peripheral system over UART1; no Nanobot counterpart |
| *(no equivalent)*           | `voice/`                       | MimiClaw-original: I2S voice I/O with VAD, STT, TTS; no Nanobot counterpart |
