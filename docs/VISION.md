# MimiClaw Vision — Pluggable AI Brain Box

> **ESP32-S3 + MimiClaw = plug-in AI brain.**
> Connect power and it's a complete AI assistant.
> Plug in a robotic arm and it controls hardware.
> Add a microphone and it speaks.

---

## One-Line Definition

An ESP32-S3 running MimiClaw is a **pluggable AI brain box**. Power it on and you have a complete AI assistant. Plug in a peripheral and the LLM automatically understands and drives it — no recompilation, no retraining.

---

## Problem Background

### Why Does This Exist?

**Cloud AI assistants are black boxes.**
Cut the internet connection and they stop working. Your data passes through servers you don't control, and you can't inspect or modify how the system thinks.

**Edge LLM deployment is expensive.**
Running a capable agent on a Raspberry Pi or Jetson means $50–$200 in hardware, a Linux stack to maintain, and power consumption that rules out battery operation. That price point does not embed into ordinary devices.

**Robot and IoT "AI brains" are tightly coupled to their hardware drivers.**
When you swap a robotic arm for a robot dog, you rewrite the firmware. The brain should be decoupled from the hardware — a clean interface where any peripheral can describe itself and get driven by the same LLM agent.

**MimiClaw's answer:** a $5 ESP32-S3 chip is the brain. Any hardware snaps onto it. The LLM automatically understands and drives whatever is connected.

---

## Three-Layer Architecture

This is the core design. Three layers, strict one-way dependency.

```
┌─────────────────────────────────────────────────────┐
│  Layer 3: Peripheral Capability Layer (hot-plug, optional)  │
│  Plug in robotic arm → +arm_move / +arm_gripper       │
│  Plug in robot dog   → +walk / +turn / +grip          │
├─────────────────────────────────────────────────────┤
│  Layer 2: Voice Interaction Layer (compile flag, optional)  │
│  With mic+speaker → voice I/O                         │
│  Without hardware → Telegram / WebSocket only         │
├─────────────────────────────────────────────────────┤
│  Layer 1: Brain Core (always runs independently)      │
│  LLM Agent + Tools + Memory + Telegram + WebSocket    │
└─────────────────────────────────────────────────────┘
```

**Key principle: Layer 1 never depends on Layer 2 or Layer 3.**

Layer 2 and Layer 3 failures are non-fatal. If I2S hardware is absent, voice is skipped — the agent keeps running. If the peripheral disconnects mid-session, its tools deregister automatically and Telegram continues working.

### Brain Box Connection Diagram

```
                    ┌──────────────────────┐
  Microphone ──I2S0─┤                      ├─UART1──► Peripheral MCU
                    │    ESP32-S3 Brain     │          (robotic arm,
     Speaker ──I2S1─┤                      │           robot dog,
                    │    MimiClaw firmware  │           sensor board…)
  Telegram / ───────┤                      │
  WebSocket         └──────────────────────┘
```

---

## Brain–Peripheral Relationship

The peripheral design follows a **declare-and-drive** model:

1. **Peripheral declares capabilities** — it ships a `manifest.json` describing its tools (name, description, JSON schema parameters) and a set of Skill `.md` files that teach the LLM how to use them.
2. **Brain handshakes via PDP protocol** — on physical connection (GPIO21 detect), the brain initiates a PDP v1 handshake over UART1, reads the manifest, transfers skill files to SPIFFS, and registers each tool in the live tool registry.
3. **LLM drives any peripheral with natural language** — the newly registered tools appear in the next API call's tool list with no recompilation. The LLM reasons about which tools to call and in what order.
4. **On disconnect, tools auto-deregister** — the brain removes peripheral tools from the registry and continues running normally. No crash, no restart.

This means the same ESP32-S3 brain can drive a robotic arm on Monday and a sensor array on Tuesday. The LLM needs no retraining; the peripheral just describes itself differently.

---

## End-to-End Scenario

> "Move the red cup on the desk to the right."

Here is the full execution path:

```
User speaks ──► INMP441 mic (I2S0)
             ──► Energy VAD detects speech
             ──► DashScope STT → text string
             ──► Agent ReAct loop begins
                  │
                  ├─ Tool call 1: arm_status()          → "ready, position (0,0,0)"
                  ├─ Tool call 2: arm_move(x=120, y=50) → "moved"
                  ├─ Tool call 3: arm_gripper(open=true) → "opened"
                  ├─ Tool call 4: arm_move(x=240, y=50) → "moved"
                  └─ Tool call 5: arm_gripper(open=false)→ "closed"
                  │
             ──► Agent generates response: "Done, the cup has been moved to the right."
             ──► DashScope TTS → PCM audio
             ──► MAX98357A speaker (I2S1) plays audio
```

Five tool calls, all serialized over UART1 to the peripheral MCU. The LLM planned the sequence from a single natural-language instruction.

---

## Design Philosophy

Six principles guide every architectural decision in MimiClaw.

### 1. Always Independent

The brain core works without any external hardware or network. The serial CLI remains functional even with WiFi down. Local tools (`read_file`, `write_file`, `get_current_time`) work without cloud access. MimiClaw is not a dumb HTTP proxy; it is a self-contained agent.

### 2. Graceful Degradation

- WiFi drops → CLI and local tools stay operational
- Peripheral disconnects → Telegram and WebSocket continue; peripheral tools silently deregister
- No microphone present → voice layer skips initialization; the rest runs normally
- LLM API unreachable → agent returns an error message; no crash, no lockup

Failures are isolated. No layer can take down a lower layer.

### 3. Zero Vendor Lock-in

- **LLM**: switchable between Anthropic (Claude) and OpenAI (GPT) via `mimi_secrets.h` or NVS CLI at runtime
- **STT/TTS**: switchable between DashScope and Whisper/other providers
- **Peripheral MCU**: any microcontroller that speaks UART and implements PDP v1 — Arduino, STM32, RP2040, bare AVR
- **Channels**: Telegram and WebSocket today; the message bus accepts new channel drivers without touching the agent

### 4. Human-Readable Storage

All persistent state is plain text on SPIFFS:

| File | Purpose |
|------|---------|
| `config/SOUL.md` | Agent personality |
| `config/USER.md` | User profile |
| `memory/MEMORY.md` | Long-term memory |
| `memory/YYYY-MM-DD.md` | Daily notes |
| `sessions/tg_<id>.jsonl` | Per-chat history |
| `cron/cron.json` | Scheduled tasks |
| `skills/peripheral_<name>.md` | Peripheral skill files |

A developer can mount the SPIFFS partition, read files with any text editor, and understand exactly what the agent knows and remembers. No binary blobs, no opaque databases.

### 5. Skills as Knowledge Carriers

Peripheral capability `.md` files are injected into the system prompt at runtime. The LLM reads them and immediately knows how to use new hardware — what the tools do, when to call them, what parameters they accept, and what side effects to expect. No recompilation. No model retraining. Swap the skill file, swap the behavior.

### 6. Minimal Hardware Cost

| Component | Target Cost |
|-----------|------------|
| ESP32-S3 module | ~$5 |
| INMP441 microphone | ~$1 |
| MAX98357A amplifier + speaker | ~$3 |
| Enclosure + power | ~$5 |
| **Complete voice box** | **< $15** |

The peripheral MCU is separate and can be as cheap as a $2 Arduino Nano. The brain does not require expensive compute hardware.

---

## Roadmap

### Completed

- ✅ **Layer 1**: Full ReAct agent, Telegram long-polling, WebSocket gateway, 9 built-in tools (`web_search`, `get_current_time`, `read_file`, `write_file`, `edit_file`, `list_dir`, `cron_add`, `cron_list`, `cron_remove`), SPIFFS memory, cron scheduler, heartbeat, skills loader
- ✅ **Layer 2**: Voice framework (`voice_input`, `voice_output`, `voice_channel`), DashScope STT/TTS, I2S hardware drivers, energy-based VAD, PTT button
- ✅ **Layer 3**: PDP v1 protocol, peripheral hot-plug detection (GPIO21), dynamic tool registration (`tool_registry_register_dynamic`), auto-deregister on disconnect, skill file transfer to SPIFFS

### Planned

- 🔲 **ESP-SR offline wake word** — "Hey Mimi" triggers listening without internet, no PTT required
- 🔲 **Reference peripheral firmware** — Arduino robotic arm demo with full PDP v1 implementation, usable as a peripheral SDK template
- 🔲 **Multi-peripheral support** — UART2 as a second peripheral interface, allowing two independent peripherals simultaneously
- 🔲 **Peripheral security verification** — Manifest signing to prevent untrusted peripherals from injecting malicious tool definitions

---

## Documentation Guide

Start here and follow the path that matches your goal:

| Document | Audience | Content |
|----------|----------|---------|
| **VISION.md** (this file) | Everyone | Why MimiClaw exists, what it does, design philosophy |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Developers | Module map, dual-core design, queue architecture, memory layout |
| [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) | Developers | Build setup, adding tools, adding channels, debugging |
| [PERIPHERAL_DESIGN.md](PERIPHERAL_DESIGN.md) | Hardware builders | PDP v1 protocol spec, manifest format, skill file format |
| [PERIPHERAL_SDK.md](PERIPHERAL_SDK.md) | Peripheral firmware authors | Step-by-step guide to implementing a PDP v1 peripheral |

If you want to **understand the system**, read ARCHITECTURE.md.
If you want to **build on top of it**, read DEVELOPER_GUIDE.md.
If you want to **connect hardware**, read PERIPHERAL_DESIGN.md and PERIPHERAL_SDK.md.
