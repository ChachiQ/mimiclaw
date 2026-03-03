# MimiClaw vs Nanobot — Feature Gap Tracker

> Comparing against `nanobot/` reference implementation. Tracks features MimiClaw has not yet aligned with.
> Priority: P0 = Core missing, P1 = Important enhancement, P2 = Nice to have

---

## P0 — Core Agent Capabilities

### [x] ~~Tool Use Loop (multi-turn agent iteration)~~
- Implemented: `agent_loop.c` ReAct loop with `llm_chat_tools()`, max 10 iterations, non-streaming JSON parsing

### [x] ~~Memory Write via Tool Use (agent-driven memory persistence)~~
- Implemented: `write_file` and `edit_file` tools allow the agent to write to `/spiffs/memory/MEMORY.md` and daily notes; system prompt instructs agent when to persist memory

### [x] ~~Tool Registry + web_search Tool~~
- Implemented: `tools/tool_registry.c` — tool registration, JSON schema builder, dispatch by name
- Implemented: `tools/tool_web_search.c` — Brave Search API via HTTPS (direct + proxy support)

### [x] ~~More Built-in Tools~~
- Implemented: `read_file`, `write_file`, `edit_file`, `list_dir` in `tools/tool_files.c`; `get_current_time` in `tools/tool_get_time.c`
- All 9 tools registered in `tools/tool_registry.c`

### [ ] Subagent / Spawn Background Tasks
- **nanobot**: `subagent.py` — SubagentManager spawns independent agent instances with isolated tool sets and system prompts, announces results back to main agent via system channel
- **MimiClaw**: Not implemented
- **Recommendation**: ESP32 memory is limited; simplify to a single background FreeRTOS task for long-running work, inject result into inbound queue on completion

---

## P1 — Important Features

### [ ] Telegram User Allowlist (allow_from)
- **nanobot**: `channels/base.py` L59-82 — `is_allowed()` checks sender_id against allow_list
- **MimiClaw**: No authentication; anyone can message the bot and consume API credits
- **Recommendation**: Store allow_from list in `mimi_secrets.h` as a build-time define, filter in `process_updates()`

### [ ] Telegram Markdown to HTML Conversion
- **nanobot**: `channels/telegram.py` L16-76 — `_markdown_to_telegram_html()` full converter: code blocks, inline code, bold, italic, links, strikethrough, lists
- **MimiClaw**: Uses `parse_mode: Markdown` directly; special characters can cause send failures (has fallback to plain text)
- **Recommendation**: Implement simplified Markdown-to-HTML converter, or switch to `parse_mode: HTML`

### [ ] Telegram /start Command
- **nanobot**: `telegram.py` L183-192 — handles `/start` command, replies with welcome message
- **MimiClaw**: Not handled; /start is sent to Claude as a regular message

### [ ] Telegram Media Handling (photos/voice/files)
- **nanobot**: `telegram.py` L194-289 — handles photo, voice, audio, document; downloads files; transcribes voice
- **MimiClaw**: Only processes `message.text`, ignores all media messages
- **Recommendation**: Images can be base64-encoded for Claude Vision; voice requires Whisper API (extra HTTPS request)

### [x] ~~Skills System (pluggable capabilities)~~
- Implemented: `skills/skill_loader.c` — loads .md files from `/spiffs/skills/`, injects skill summaries into system prompt via `context_builder`
- Built-in skills (weather, daily_briefing, skill_creator) seeded to SPIFFS on first boot
- CLI commands: `skill_list`, `skill_show`, `skill_search`

### [ ] Full Bootstrap File Alignment
- **nanobot**: Loads `AGENTS.md`, `SOUL.md`, `USER.md`, `TOOLS.md`, `IDENTITY.md` (5 files)
- **MimiClaw**: Only loads `SOUL.md` and `USER.md`
- **Recommendation**: Add AGENTS.md (behavior guidelines) and TOOLS.md (tool documentation)

### [ ] Longer Memory Lookback
- **nanobot**: `memory.py` L56-80 — `get_recent_memories(days=7)` defaults to 7 days
- **MimiClaw**: `context_builder.c` only reads last 3 days
- **Recommendation**: Make configurable, but mind token budget

### [x] ~~System Prompt Tool Guidance~~
- Implemented: `context_builder.c` includes tool usage guidance in system prompt

### [ ] Message Metadata (media, reply_to, metadata)
- **nanobot**: `bus/events.py` — InboundMessage has media, metadata fields; OutboundMessage has reply_to
- **MimiClaw**: `mimi_msg_t` only has channel + chat_id + content
- **Recommendation**: Extend msg struct, add media_path and metadata fields

### [ ] Outbound Subscription Pattern
- **nanobot**: `bus/queue.py` L41-49 — supports `subscribe_outbound(channel, callback)` subscription model
- **MimiClaw**: Hardcoded if-else dispatch
- **Recommendation**: Current approach is simple and reliable; not worth changing with few channels

---

## P2 — Advanced Features

### [x] ~~Cron Scheduled Task Service~~
- Implemented: `cron/cron_service.c` — FreeRTOS task, `every` (recurring) + `at` (one-shot) schedule types
- Jobs persisted to `/spiffs/cron.json`, survive reboots
- Agent tools: `cron_add`, `cron_list`, `cron_remove`; CLI: `cron_start`

### [x] ~~Heartbeat Service~~
- Implemented: `heartbeat/heartbeat.c` — FreeRTOS timer fires every 30 minutes
- Reads `/spiffs/HEARTBEAT.md`, triggers agent turn if actionable tasks found (skips empty lines, headers, completed `[x]` checkboxes)
- CLI: `heartbeat_trigger` for manual check

### [ ] Multi-LLM Provider Support
- **nanobot**: `providers/litellm_provider.py` — supports OpenRouter, Anthropic, OpenAI, Gemini, DeepSeek, Groq, Zhipu, vLLM via LiteLLM
- **MimiClaw**: Hardcoded to Anthropic Messages API
- **Recommendation**: Abstract LLM interface, support OpenAI-compatible API (most providers are compatible)

### [ ] Voice Transcription
- **nanobot**: `providers/transcription.py` — Groq Whisper API
- **MimiClaw**: Not implemented
- **Recommendation**: Requires extra HTTPS request to Whisper API: download Telegram voice -> forward -> get text

### [x] ~~Build-time Config File + Runtime NVS Override~~
- Implemented: `mimi_secrets.h` as build-time defaults, NVS as runtime override via CLI
- Two-layer config: NVS priority → build-time fallback; CLI: `set_wifi`, `set_api_key`, `set_model`, `set_proxy`, etc.
- `config_show` displays effective values with source (NVS or build); `config_reset` wipes NVS

### [ ] WebSocket Gateway Protocol Enhancement
- **nanobot**: Gateway port 18790 + richer protocol
- **MimiClaw**: Basic JSON protocol, lacks streaming token push
- **Recommendation**: Add `{"type":"token","content":"..."}` streaming push

### [ ] Multi-Channel Manager
- **nanobot**: `channels/manager.py` — unified lifecycle management for multiple channels
- **MimiClaw**: Hardcoded in app_main()
- **Recommendation**: Not worth abstracting with few channels

### [ ] WhatsApp / Feishu Channels
- **nanobot**: `channels/whatsapp.py`, `channels/feishu.py`
- **MimiClaw**: Only Telegram + WebSocket
- **Recommendation**: Low priority, Telegram is sufficient

### [x] ~~Telegram Proxy Support (HTTP CONNECT)~~
- Implemented: HTTP CONNECT tunnel via `proxy/http_proxy.c`, configurable via `mimi_secrets.h` (`MIMI_SECRET_PROXY_HOST`/`MIMI_SECRET_PROXY_PORT`)

### [ ] Session Metadata Persistence
- **nanobot**: `session/manager.py` L136-153 — session file includes metadata line (created_at, updated_at)
- **MimiClaw**: JSONL only stores role/content/ts, no metadata header
- **Recommendation**: Low priority

---

## Completed Alignment

- [x] Telegram Bot long polling (getUpdates)
- [x] Message Bus (inbound/outbound queues)
- [x] Agent Loop with ReAct tool use (multi-turn, max 10 iterations)
- [x] Claude API (Anthropic Messages API, non-streaming, tool_use protocol)
- [x] OpenAI-compatible API support (`set_model_provider openai`)
- [x] Tool Registry + web_search tool (Brave Search API)
- [x] Built-in tools: `get_current_time`, `read_file`, `write_file`, `edit_file`, `list_dir`
- [x] Cron tools: `cron_add`, `cron_list`, `cron_remove` — agent-schedulable recurring/one-shot tasks
- [x] Memory Write via Tool Use (agent uses `write_file`/`edit_file` to persist MEMORY.md)
- [x] Context Builder (system prompt + bootstrap files + memory + skill summaries + tool guidance)
- [x] Memory Store (MEMORY.md + daily notes)
- [x] Session Manager (JSONL per chat_id, ring buffer history)
- [x] WebSocket Gateway (port 18789, JSON protocol)
- [x] Serial CLI (esp_console, full config + debug/maintenance commands)
- [x] HTTP CONNECT Proxy + SOCKS5 (Telegram + LLM API + Brave Search via proxy tunnel)
- [x] OTA Update
- [x] WiFi Manager (build-time credentials + NVS override, exponential backoff)
- [x] SPIFFS storage
- [x] Build-time config (`mimi_secrets.h`) + runtime NVS override via CLI
- [x] Skills System (SPIFFS .md files, injected into system prompt, CLI management)
- [x] Cron Scheduler (FreeRTOS task, every/at jobs, persisted to cron.json)
- [x] Heartbeat Service (FreeRTOS timer, 30-min check of HEARTBEAT.md)

---

## Suggested Implementation Order

```
1. [done] Tool Use Loop + Tool Registry + web_search
2. [done] Memory Write via Tool Use (write_file/edit_file tools)
3. [done] Built-in Tools (read_file, write_file, edit_file, list_dir, get_current_time)
4. [done] Cron Scheduler + Heartbeat Service
5. [done] Skills System
6. [done] Runtime NVS Config (set_wifi, set_api_key, etc.)
7. Telegram Allowlist (allow_from)   <- security essential
8. Bootstrap File Completion (AGENTS.md, TOOLS.md)
9. Telegram Markdown -> HTML
10. Subagent (simplified)
11. Media Handling
12. Other enhancements
```
