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
   a. Route by channel field ("telegram" → sendMessage, "websocket" → WS frame)
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
│   └── tool_cron.h/c       Cron job management (cron_add, cron_list, cron_remove)
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
/spiffs/config/SOUL.md          AI personality definition
/spiffs/config/USER.md          User profile
/spiffs/memory/MEMORY.md        Long-term persistent memory
/spiffs/memory/2026-02-05.md    Daily notes (one file per day)
/spiffs/sessions/tg_12345.jsonl Session history (one file per Telegram chat)
/spiffs/cron.json               Persisted cron job definitions
/spiffs/HEARTBEAT.md            Pending tasks checked every 30 minutes
/spiffs/skills/weather.md       Built-in skill (loaded on first boot)
/spiffs/skills/daily_briefing.md Built-in skill
/spiffs/skills/skill_creator.md  Built-in skill
/spiffs/skills/<custom>.md      Custom skills added by the agent or user
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
    char channel[16];   // "telegram", "websocket", "cli"
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
    {"type": "tool_use", "id": "toolu_xxx", "name": "web_search", "input": {"query": "weather today"}}
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

---

## Startup Sequence

```
app_main()
  ├── init_nvs()                    NVS flash init (erase if corrupted)
  ├── esp_event_loop_create_default()
  ├── init_spiffs()                 Mount SPIFFS at /spiffs
  ├── message_bus_init()            Create inbound + outbound queues
  ├── memory_store_init()           Verify SPIFFS paths
  ├── skill_loader_init()           Load built-in skills to SPIFFS, scan /spiffs/skills/
  ├── session_mgr_init()
  ├── wifi_manager_init()           Init WiFi STA mode + event handlers
  ├── http_proxy_init()             Load proxy config (NVS → build-time fallback)
  ├── telegram_bot_init()           Load bot token (NVS → build-time fallback)
  ├── llm_proxy_init()              Load API key + model (NVS → build-time fallback)
  ├── tool_registry_init()          Register 9 built-in tools, build tools JSON
  ├── cron_service_init()           Load persisted cron jobs from /spiffs/cron.json
  ├── heartbeat_init()              Create FreeRTOS timer (30 min interval)
  ├── agent_loop_init()
  ├── serial_cli_init()             Start REPL (works without WiFi)
  │
  ├── wifi_manager_start()          Connect using NVS credentials (fallback to build-time)
  │   └── wifi_manager_wait_connected(30s)
  │
  └── [if WiFi connected]
      ├── outbound_dispatch task    Launch outbound task (Core 0) — first to avoid drops
      ├── agent_loop_start()        Launch agent_loop task (Core 1)
      ├── telegram_bot_start()      Launch tg_poll task (Core 0)
      ├── cron_service_start()      Launch cron task (any core, prio 4)
      ├── heartbeat_start()         Start 30-min FreeRTOS timer
      └── ws_server_start()         Start httpd on port 18789
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
