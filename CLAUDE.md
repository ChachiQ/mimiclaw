# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Role

When working in this repository, operate as a **C/C++ embedded-systems expert** with deep knowledge of:
- FreeRTOS task/queue/semaphore primitives
- ESP-IDF APIs (esp_http_client, SPIFFS, NVS, esp-tls, esp_timer, console)
- ESP32-S3 memory layout (DRAM, IRAM, PSRAM / SPIRAM)
- Bare-metal C patterns: fixed-size buffers, zero dynamic allocation in hot paths, careful stack sizing

## Project Overview

MimiClaw is an embedded AI assistant firmware for the ESP32-S3 microcontroller (~$5). It is written entirely in C (no Linux, no Node.js) and runs on FreeRTOS. It connects via Telegram or WebSocket, runs a ReAct agent loop against Claude or GPT, executes tools (web search, cron, file I/O), and persists memory on SPIFFS flash storage.

## Project Lineage

MimiClaw is a **minimal ESP32-S3 port of the openClaw project**. openClaw defines the "claw-style" AI agent architecture: a ReAct loop with a tool registry, persistent memory files, and a message-bus that decouples input channels from the agent core. MimiClaw preserves this architecture but strips it down to fit on a $5 microcontroller — no Linux, no Node.js, pure C on FreeRTOS.

When making architectural decisions, follow openClaw's design philosophy: clean module boundaries, registry-driven extensibility (tools, skills), and plain-text SPIFFS files as the persistence layer.

## Build Commands

Requires ESP-IDF v5.5+ installed and sourced. Use the setup scripts to install it:

```bash
# macOS
./scripts/setup_idf_macos.sh
./scripts/build_macos.sh

# Ubuntu
./scripts/setup_idf_ubuntu.sh
./scripts/build_ubuntu.sh

# Manual (after sourcing IDF)
idf.py set-target esp32s3
idf.py fullclean && idf.py build

# Flash and monitor (USB JTAG/UART port required)
idf.py -p /dev/cu.usbmodem11401 flash monitor
```

There are no automated tests — validation is manual on hardware. CI runs `idf.py fullclean && idf.py build` to verify compilation.

## Configuration

Copy `main/mimi_secrets.h.example` to `main/mimi_secrets.h` and fill in credentials (WiFi, Telegram bot token, Anthropic/OpenAI key). This file is gitignored. Build-time constants (stack sizes, timeouts, defaults) live in `main/mimi_config.h`.

## Architecture

### Dual-Core Design

- **Core 0**: Telegram long-polling, WebSocket server, outbound dispatch, serial CLI
- **Core 1**: AI agent loop (ReAct)

Two FreeRTOS queues (`inbound_queue`, `outbound_queue`) defined in `main/bus/` decouple input channels from agent processing.

### Agent Loop (`main/agent/`)

1. Pop `mimi_msg_t` from inbound queue
2. Load session history from SPIFFS (`main/memory/session_mgr`)
3. Build system prompt from SOUL.md + USER.md + MEMORY.md + daily notes (`context_builder`)
4. ReAct loop (max 10 iterations): call LLM → parse tool_use blocks → execute tools → loop until `stop_reason != "tool_use"`
5. Save updated conversation to SPIFFS
6. Push response to outbound queue

### LLM Layer (`main/llm/`)

`llm_proxy` handles HTTPS requests to Anthropic Messages API or OpenAI Chat API. Supports both providers, switchable via `mimi_secrets.h`. Handles chunked transfer decoding. Tool definitions are serialized as JSON schema and sent with each request.

### Tool Registry (`main/tools/`)

Registry-based dispatch. Tools register with name + JSON schema. Available tools: `web_search` (Brave API), `cron_add/remove`, file read/write, time retrieval. Up to 4 parallel tool calls per iteration.

### Persistent Storage (`main/memory/`)

All state is stored as plain text on a 12 MB SPIFFS partition:
- `config/SOUL.md`, `config/USER.md` — agent personality/user profile
- `memory/MEMORY.md` — long-term memory
- `memory/YYYY-MM-DD.md` — daily notes
- `sessions/tg_<chat_id>.jsonl` — per-chat message history
- `cron/cron.json` — scheduled tasks

### Other Modules

| Module | Location | Purpose |
|--------|----------|---------|
| WiFi | `main/wifi/` | STA lifecycle, exponential backoff reconnect |
| Telegram | `main/telegram/` | Long polling, message parse, send |
| WebSocket gateway | `main/gateway/` | LAN client connections |
| Serial CLI | `main/cli/` | REPL for config/debug over USB |
| HTTP proxy | `main/proxy/` | CONNECT tunnel for restricted networks |
| Cron scheduler | `main/cron/` | Persisted scheduled tasks |
| Heartbeat | `main/heartbeat/` | Autonomous agent triggers via HEARTBEAT.md |
| Skills | `main/skills/` | Pluggable skill loader from SPIFFS |
| OTA | `main/ota/` | Firmware updates over WiFi |

## Key Constraints

- **Pure C, ESP-IDF only** — no external libraries beyond ESP-IDF components
- **PSRAM required** — large buffers (HTTP response, cJSON trees) use `heap_caps_malloc(MALLOC_CAP_SPIRAM)`
- **No heap fragmentation** — avoid frequent small allocs/frees in the agent loop
- **Single CMake component** — all 22 source files registered in `main/CMakeLists.txt`

## Commit Style

Follow conventional commits: `feat:`, `fix:`, `docs:`, `refactor:`, `chore:`
