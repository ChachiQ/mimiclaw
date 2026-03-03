# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Role

When working in this repository, operate as a **C/C++ embedded-systems expert** with deep knowledge of:
- FreeRTOS task/queue/semaphore primitives
- ESP-IDF APIs (esp_http_client, SPIFFS, NVS, esp-tls, esp_timer, console)
- ESP32-S3 memory layout (DRAM, IRAM, PSRAM / SPIRAM)
- Bare-metal C patterns: fixed-size buffers, zero dynamic allocation in hot paths, careful stack sizing

## Project Overview

MimiClaw is a **pluggable AI Brain Box** firmware for the ESP32-S3 microcontroller (~$5). Written entirely in C (no Linux, no Node.js) on FreeRTOS, it implements a three-layer modular architecture:

- **Layer 1 (Brain Core)**: ReAct LLM agent, Telegram, WebSocket, 9 built-in tools, SPIFFS memory, Cron, Heartbeat — always runs independently
- **Layer 2 (Voice, optional)**: I2S INMP441 mic + MAX98357A speaker, DashScope STT/TTS — compile-time enable/disable, non-fatal init
- **Layer 3 (Peripheral, hot-plug)**: Any MCU connects via UART1 (GPIO17/18), declares tools via PDP v1 protocol, gets auto-registered as LLM tools — non-fatal hot-plug

**Invariant: Layer 1 never depends on Layer 2 or Layer 3.**

## Project Lineage

MimiClaw started as a minimal ESP32-S3 port of the openClaw project (claw-style ReAct agent with tool registry, SPIFFS memory, message bus). It has since evolved its own identity around the **three-layer independent model**:

- Layer 1 independence: the brain core must work without any peripheral or voice hardware
- Peripheral hot-swap: connect/disconnect any UART peripheral with zero impact on brain operation
- Voice as optional upgrade: compile out entirely when hardware is absent

When making architectural decisions, apply MimiClaw's design principles: layer independence, graceful degradation, human-readable SPIFFS storage, and registry-driven extensibility.

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
| Peripheral | `main/peripheral/` | PDP v1 hot-plug: UART1 driver, GPIO21 detector, protocol, manager |
| Voice | `main/voice/` | I2S mic (STT) + speaker (TTS), state machine, DashScope APIs |

### Three-Layer Model

| Layer | Description | Non-fatal? |
|-------|-------------|------------|
| Layer 1: Brain Core | LLM agent, tools, memory, Telegram, WebSocket | N/A (always runs) |
| Layer 2: Voice | I2S mic/speaker, STT/TTS | Yes — brain continues without it |
| Layer 3: Peripheral | UART hot-plug, PDP v1, dynamic tools | Yes — brain continues without it |

### Tool Registry Dynamic API

`tool_registry_register_dynamic(name, desc, schema, fn)` — registers a tool at runtime (used by peripheral subsystem).
`unregister_peripheral_tools()` — removes all peripheral-registered tools.
`rebuild_json()` — rebuilds the tools JSON array sent to LLM (call after any dynamic register/unregister).

Universal peripheral tool stub: `tools/tool_peripheral.c/h` — all peripheral tools share one execute function that sends UART RPC to the peripheral and waits for response.

## Key Constraints

- **Pure C, ESP-IDF only** — no external libraries beyond ESP-IDF components
- **PSRAM required** — large buffers (HTTP response, cJSON trees) use `heap_caps_malloc(MALLOC_CAP_SPIRAM)`
- **No heap fragmentation** — avoid frequent small allocs/frees in the agent loop
- **Single CMake component** — all 27+ source files registered in `main/CMakeLists.txt`
- **Layer 1 independence** — agent_loop, telegram, websocket must never import peripheral/ or voice/ headers
- **minimp3 exception** — single-header library in main/ for MP3 decode; only approved external header outside ESP-IDF

## Commit Style

Follow conventional commits: `feat:`, `fix:`, `docs:`, `refactor:`, `chore:`
