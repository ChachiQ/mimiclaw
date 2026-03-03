# MimiClaw Roadmap

> MimiClaw is a pluggable AI Brain Box on ESP32-S3. This roadmap tracks milestones across the three-layer modular architecture.

---

## ✅ Milestone 1 — Layer 1: Brain Core (Complete)

The foundational layer. Runs independently, never depends on Layer 2 or Layer 3.

- [x] ReAct agent loop (max 10 iterations, multi-turn tool use)
- [x] Anthropic Claude API support (non-streaming, tool_use protocol)
- [x] OpenAI-compatible API support (switchable at runtime)
- [x] Tool Registry — 9 built-in tools with JSON Schema dispatch
  - [x] `web_search` (Brave Search API)
  - [x] `get_current_time` (NTP-synced)
  - [x] `read_file`, `write_file`, `edit_file`, `list_dir` (SPIFFS I/O)
  - [x] `cron_add`, `cron_list`, `cron_remove`
- [x] Telegram long-polling channel
- [x] WebSocket gateway (port 18789)
- [x] Serial CLI (esp_console, config + debug commands)
- [x] SPIFFS persistent storage (SOUL.md, USER.md, MEMORY.md, daily notes, sessions)
- [x] Session Manager (JSONL per chat_id, ring buffer history)
- [x] Two-layer config (build-time mimi_secrets.h + runtime NVS override)
- [x] Skills System (SPIFFS .md files, injected into system prompt)
- [x] Cron Scheduler (FreeRTOS task, every/at jobs, persisted to cron.json)
- [x] Heartbeat Service (FreeRTOS timer, 30-min check of HEARTBEAT.md)
- [x] HTTP CONNECT proxy + SOCKS5 support
- [x] OTA firmware update
- [x] WiFi Manager (exponential backoff reconnect)
- [x] Dynamic tool registration API (`tool_registry_register_dynamic`)

---

## ✅ Milestone 2 — Layer 2: Voice Subsystem (Framework Complete)

Compile-optional voice I/O layer. Both init failures are non-fatal.

- [x] Voice input: I2S0 INMP441 microphone (GPIO4/5/6)
- [x] Energy-based VAD (voice activity detection)
- [x] PTT (push-to-talk) via GPIO0
- [x] DashScope STT (speech-to-text) API integration
- [x] Voice output: I2S1 MAX98357A speaker (GPIO15/16/7)
- [x] DashScope CosyVoice TTS (text-to-speech) API integration
- [x] PCM audio playback via I2S1
- [x] Voice channel state machine (IDLE/LISTENING/PROCESSING/SPEAKING)
- [x] Voice responses routed via outbound_queue (channel="voice")
- [ ] ESP-SR offline wake word ("Hey Mimi") — no cloud round-trip required
- [ ] Local VAD improvement (WebRTC VAD or ESP-SR VAD)
- [ ] Streaming TTS (reduce first-word latency)

---

## ✅ Milestone 3 — Layer 3: Peripheral Subsystem (Framework Complete)

Hot-plug peripheral capability layer. Peripheral disconnect is non-fatal.

- [x] UART1 driver (GPIO17=TX, GPIO18=RX, 115200 baud)
- [x] GPIO21 hot-plug detection (50ms debounce, edge ISR)
- [x] PDP v1 (Peripheral Declaration Protocol)
  - [x] Handshake (hello/hello_ack)
  - [x] Manifest exchange (tool name + description + schema)
  - [x] Skill file transfer (peripheral → brain → SPIFFS)
  - [x] Tool RPC over UART (brain calls tool → UART → peripheral executes)
- [x] Peripheral Manager (connect→register, disconnect→unregister lifecycle)
- [x] Universal UART RPC stub (`tool_peripheral.c`)
- [x] Dynamic tool registration/unregistration
- [x] tools JSON rebuild after peripheral connect/disconnect
- [ ] Reference peripheral firmware (Arduino/ESP32 robotic arm demo)
- [ ] Multi-peripheral support (UART2 as second interface)
- [ ] Peripheral security (Manifest signing / authentication)
- [ ] PDP v2: bidirectional events (peripheral → brain push notifications)

---

## 🔲 Milestone 4 — Security & Stability

- [ ] Telegram user allowlist (`allow_from` build-time config)
- [ ] Peripheral Manifest signing (prevent rogue peripherals)
- [ ] NVS encryption for API keys
- [ ] Watchdog integration for agent_loop task
- [ ] Stack high-water mark monitoring via CLI

---

## 🔲 Milestone 5 — Enhanced Agent Capabilities

- [ ] Telegram Markdown → HTML conversion (fix special character send failures)
- [ ] Telegram `/start` command handler
- [ ] Extended bootstrap files (AGENTS.md behavior guidelines, TOOLS.md documentation)
- [ ] Longer memory lookback (configurable, currently 3 days)
- [ ] Simplified subagent (background FreeRTOS task, result injected to inbound queue)
- [ ] Telegram media handling (photos for Claude Vision, voice for Whisper)

---

## 🔲 Milestone 6 — Ecosystem

- [ ] Reference peripheral firmware repository (robotic arm Arduino demo)
- [ ] Peripheral SDK documentation (how to build a PDP-compatible peripheral)
- [ ] Pre-built binary releases (no IDF toolchain needed for end users)
- [ ] Hardware reference design (BOM + schematic for complete Brain Box)
- [ ] OTA for peripheral firmware (brain coordinates peripheral updates)
- [ ] Multi-LLM provider support (DeepSeek, Groq, local Ollama via HTTP)

---

## Deferred / Won't Fix

- WhatsApp / Feishu channels — low priority, Telegram sufficient
- WebSocket streaming token push — not worth complexity
- Session metadata persistence — low value
- Outbound subscription pattern — current if-else is fine for few channels
