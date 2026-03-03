# MimiClaw Peripheral Design

> Technical design document for the MimiClaw modular brain / peripheral system.
> Target hardware: ESP32-S3 Brain Box + external peripheral MCUs.

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Physical Connection Specification](#2-physical-connection-specification)
3. [Software Module Architecture](#3-software-module-architecture)
4. [PDP v1 Protocol Specification](#4-pdp-v1-protocol-specification)
5. [Manifest Format Specification](#5-manifest-format-specification)
6. [Data Flow Architecture](#6-data-flow-architecture)
7. [SPIFFS Storage Paths](#7-spiffs-storage-paths)
8. [Design Principles](#8-design-principles)
9. [mimi_config.h Constants Reference](#9-mimi_configh-constants-reference)

---

## 1. System Overview

MimiClaw uses a three-layer architecture that separates the AI intelligence core from optional hardware extensions.

### Layer Model

```
Layer 3: Peripheral Devices (hot-plug, optional)
  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────────┐
  │  Robotic Arm    │  │  Sensor Array   │  │  Display Module  │
  │  (STM32/ESP32)  │  │  (Arduino Mega) │  │  (RP2040)        │
  └────────┬────────┘  └────────┬────────┘  └────────┬─────────┘
           │                   │                     │
           └───────────────────┴─────────────────────┘
                           6-pin magnetic connector (PDP)
                                       │
Layer 2: Voice I/O (optional hardware, compile-time enable)
  ┌────────────────────────────────────┴─────────────────────────┐
  │  INMP441 MEMS Mic (I2S)         MAX98357A Speaker (I2S)      │
  │  GPIO 4/5/6                     GPIO 15/16/7                 │
  └────────────────────────────────────┬─────────────────────────┘
                                       │
Layer 1: Core Brain (always runs, always autonomous)
  ┌────────────────────────────────────┴─────────────────────────┐
  │                     ESP32-S3 Brain Box                       │
  │                                                              │
  │  Core 0 (I/O)                   Core 1 (AI Agent)           │
  │  ┌──────────────┐               ┌────────────────────────┐  │
  │  │ Telegram Poll│               │     Agent Loop         │  │
  │  │ WS Server    │──inbound_q───▶│  ReAct + LLM Proxy     │  │
  │  │ Serial CLI   │               │  Tool Registry (9+N)   │  │
  │  │ Peripheral   │◀──outbound_q──│  Context Builder       │  │
  │  │  Manager     │               └────────────────────────┘  │
  │  └──────────────┘                                           │
  │                                                              │
  │  SPIFFS (12 MB)                                              │
  │  config/ memory/ sessions/ skills/ peripheral/ cron.json    │
  └──────────────────────────────────────────────────────────────┘
```

### Brain Box: Always Independent

Layer 1 is self-contained. The Brain Box runs Telegram polling, the WebSocket server, and the full ReAct agent loop regardless of whether any peripheral or voice hardware is present. A peripheral connection extends capabilities; it never enables them.

### What Peripherals Add

When a peripheral connects:

- The peripheral announces itself via PDP handshake
- Its tools (e.g. `arm_move`, `arm_grip`) are dynamically registered into the tool registry
- Its skill files are written to SPIFFS and included in the system prompt
- The agent can immediately start using the new tools without reboot

When a peripheral disconnects:

- Its tools are unregistered from the tool registry
- Its skill files are removed from the active skill set
- The Brain Box continues operating normally with the remaining tools

---

## 2. Physical Connection Specification

### 6-Pin Magnetic Connector Pinout

```
Pin  Signal  Direction          GPIO     Description
─────────────────────────────────────────────────────────────────
 1   VCC     Brain → Periph     —        3.3 V power supply (up to 300 mA)
 2   GND     —                  —        Common ground
 3   TX      Brain → Periph     GPIO 17  UART1 transmit (brain sends commands)
 4   RX      Periph → Brain     GPIO 18  UART1 receive (brain receives results)
 5   DET     Periph → Brain     GPIO 21  Detect: floating/low = disconnected,
                                         pulled high by peripheral = connected
 6   RST     Brain → Periph     —        Optional open-drain reset line
                                         (brain pulls low to reset peripheral)
```

Magnetic connectors are recommended for hot-plug safety (e.g. POGO-pin or magnetic USB-C breakout). The pin order places power pins (1, 2) at the physical extremes of the connector and signal pins in the middle, reducing the chance of power shorts during mating.

### UART Parameters

| Parameter         | Value           |
|-------------------|-----------------|
| Port              | UART1           |
| Baud rate         | 115,200         |
| Data bits         | 8               |
| Parity            | None            |
| Stop bits         | 1               |
| Hardware flow     | None (no RTS/CTS) |
| Logic level       | 3.3 V           |
| Max frame size    | 4,096 bytes     |
| Frame delimiter   | `\n` (0x0A)     |

UART0 (GPIO 43/44) is the USB-JTAG console used by the serial CLI — do not use it for peripheral communication.

### DET Pin Behavior

```
Brain GPIO 21 is configured with:
  - Internal pull-down resistor enabled
  - GPIO interrupt on rising edge (connect) and falling edge (disconnect)

Peripheral side:
  - Pull DET high (to 3.3 V) through a 10 kΩ resistor when powered
  - This creates a clean signal transition on connect/disconnect
```

### Cable Length

Maximum recommended cable length is 30 cm at 115,200 baud with 3.3 V logic. For longer runs, reduce baud rate or add RS-485 level converters.

---

## 3. Software Module Architecture

### New Modules

#### `main/peripheral/peripheral_uart.h/c`

UART1 driver. Initializes GPIO 17/18 as UART1 TX/RX, configures 115200-8N1, and provides line-oriented read/write functions.

```c
esp_err_t peripheral_uart_init(void);
esp_err_t peripheral_uart_write_line(const char *json_line);
esp_err_t peripheral_uart_read_line(char *buf, size_t buf_size, uint32_t timeout_ms);
void      peripheral_uart_flush(void);
```

Internal 4 KB ring buffer (`MIMI_PERIPH_UART_BUF_SIZE`) with event-driven receive via `uart_driver_install`. Frames are delimited by `\n`. `peripheral_uart_read_line` blocks until a newline is received or timeout expires.

#### `main/peripheral/peripheral_detector.h/c`

GPIO interrupt-based hot-plug detection on GPIO 21. Debounces the DET signal over 50 ms to ignore transient magnet contact bouncing.

```c
esp_err_t peripheral_detector_init(void);
bool      peripheral_detector_is_connected(void);

/* Callback type for connect/disconnect events */
typedef void (*peripheral_event_cb_t)(bool connected);
void peripheral_detector_set_callback(peripheral_event_cb_t cb);
```

Interrupt fires on both edges. A one-shot FreeRTOS timer handles debounce: the final stable state after 50 ms is reported to the registered callback.

#### `main/peripheral/peripheral_protocol.h/c`

PDP v1 protocol state machine. Implements the complete handshake sequence: hello → ack → manifest exchange → skill transfer → ready acknowledgment.

```c
typedef struct {
    char  name[64];
    char  display_name[128];
    char  version[16];
    int   tool_count;
    int   skill_count;
    char  manifest_json[4096];   /* full manifest, PSRAM-allocated */
} pdp_peer_info_t;

esp_err_t pdp_handshake(pdp_peer_info_t *out_peer);
esp_err_t pdp_request_manifest(pdp_peer_info_t *peer);
esp_err_t pdp_request_skill(int index, char *skill_name_out,
                             char *skill_content_out, size_t content_size);
esp_err_t pdp_send_ready(void);
esp_err_t pdp_send_tool_call(const char *id, const char *tool_name,
                              const char *input_json);
esp_err_t pdp_recv_tool_result(char *id_out, bool *ok_out,
                                char *output_out, size_t output_size,
                                uint32_t timeout_ms);
```

All JSON frames are constructed with `cJSON` and sent via `peripheral_uart_write_line`. Received frames are parsed with `cJSON_Parse`.

#### `main/peripheral/peripheral_manager.h/c`

Peripheral lifecycle manager. Runs as a FreeRTOS task on Core 0. Listens to connect/disconnect events from `peripheral_detector`, drives the PDP handshake, writes skill files to SPIFFS, registers/unregisters tools in the tool registry, and triggers a tools-JSON rebuild.

```c
esp_err_t peripheral_manager_init(void);
esp_err_t peripheral_manager_start(void);
bool      peripheral_manager_is_ready(void);
const char *peripheral_manager_get_name(void);

/* Called by tool_peripheral.c to send a UART RPC call */
esp_err_t peripheral_manager_tool_call(const char *tool_name,
                                        const char *input_json,
                                        char *output, size_t output_size);
```

The manager task runs a state machine:

```
IDLE ──connect──▶ HANDSHAKING ──success──▶ READY ──disconnect──▶ IDLE
                       │                                │
                       └──timeout/error──▶ ERROR ───────┘
                                (logs and returns to IDLE)
```

Init failures (UART errors, handshake timeout) are logged as warnings and the manager returns to IDLE — they never abort `app_main`.

#### `main/tools/tool_peripheral.h/c`

Dynamic tool RPC stub. For each tool announced in a peripheral manifest, `peripheral_manager` creates a `mimi_tool_t` whose `execute` function pointer calls `peripheral_manager_tool_call`. This routes the call over UART and blocks until a result arrives or `MIMI_PERIPH_TOOL_TIMEOUT_MS` (10 s) elapses.

```c
/* Called by peripheral_manager to create a stub for one manifest tool */
mimi_tool_t *tool_peripheral_create_stub(const char *tool_name,
                                          const char *description,
                                          const char *input_schema_json);
void tool_peripheral_free_stub(mimi_tool_t *stub);
```

The stub's execute function:

```c
static esp_err_t peripheral_tool_execute(const char *input_json,
                                          char *output, size_t output_size)
{
    return peripheral_manager_tool_call(tool_name, input_json,
                                        output, output_size);
}
```

#### `main/voice/voice_input.h/c`

Microphone capture via I2S (INMP441, I2S_NUM_0, GPIO 4/5/6). Records 16 kHz 16-bit mono PCM. Implements voice activity detection (VAD): stops recording after `MIMI_VOICE_VAD_SILENCE_MS` (1500 ms) of silence or at `MIMI_VOICE_MAX_REC_MS` (10 s). Sends audio to STT provider (DashScope paraformer or OpenAI Whisper) via HTTPS and returns transcript text.

```c
esp_err_t voice_input_init(void);
esp_err_t voice_input_start(void);   /* launch FreeRTOS task */
esp_err_t voice_input_record_and_transcribe(char *text_out, size_t text_size);
```

Recording buffer (`MIMI_VOICE_REC_BUF_SIZE` = 160 KB) allocated from PSRAM.

#### `main/voice/voice_output.h/c`

Text-to-speech synthesis via STT provider (DashScope CosyVoice or OpenAI TTS), then MP3/PCM playback via I2S (MAX98357A, I2S_NUM_1, GPIO 15/16/7). Uses minimp3 single-header library for MP3 decode.

```c
esp_err_t voice_output_init(void);
esp_err_t voice_output_start(void);  /* launch FreeRTOS task */
esp_err_t voice_output_speak(const char *text);
```

#### `main/voice/voice_channel.h/c`

Voice channel coordinator. Listens for push-to-talk button press (GPIO 0), triggers `voice_input_record_and_transcribe`, pushes the transcript as a `mimi_msg_t` on the inbound queue with `channel = MIMI_CHAN_VOICE`. The outbound dispatch task routes `"voice"` messages to `voice_output_speak`.

```c
esp_err_t voice_channel_init(void);
esp_err_t voice_channel_start(void);
```

### Modified Modules

#### `main/tools/tool_registry.c`

Added `is_dynamic` field to `mimi_tool_t` (already present in `tool_registry.h`). Added `tool_registry_register_dynamic`, `tool_registry_unregister_peripheral_tools`, and `tool_registry_rebuild_json` APIs used by `peripheral_manager` to add/remove tools at runtime without reboot.

Change summary: the static tool table gains a runtime-mutable section. The tools JSON string is rebuilt after each dynamic registration/unregistration event.

#### `main/mimi.c`

Added initialization calls for peripheral and voice subsystems after the existing init sequence. Both are guarded to make peripheral/voice failures non-fatal:

```c
/* Peripheral subsystem (non-fatal) */
if (peripheral_manager_init() == ESP_OK) {
    peripheral_manager_start();
}

/* Voice subsystem (non-fatal, only if CONFIG_MIMI_VOICE_ENABLE=y) */
#if CONFIG_MIMI_VOICE_ENABLE
if (voice_channel_init() == ESP_OK) {
    voice_channel_start();
}
#endif
```

Added `"voice"` channel routing in `outbound_dispatch_task`:

```c
} else if (strcmp(msg.channel, MIMI_CHAN_VOICE) == 0) {
    voice_output_speak(msg.content);
}
```

#### `main/agent/context_builder.c`

Skill summaries already include all files under `/spiffs/skills/`. Peripheral skills (written as `/spiffs/skills/peripheral_<name>.md` on connect) are automatically included in the next context build with no code changes required.

#### `main/CMakeLists.txt`

New source files to add to `SRCS`:

```cmake
"peripheral/peripheral_uart.c"
"peripheral/peripheral_detector.c"
"peripheral/peripheral_protocol.c"
"peripheral/peripheral_manager.c"
"tools/tool_peripheral.c"
"voice/voice_input.c"
"voice/voice_output.c"
"voice/voice_channel.c"
```

New IDF components to add to `REQUIRES`:

```cmake
driver   # GPIO interrupt, UART driver, I2S driver
```

---

## 4. PDP v1 Protocol Specification

PDP (Peripheral Device Protocol) is a newline-delimited JSON protocol over UART1.

### Frame Format

```
<JSON object>\n
```

- Every message is one JSON object terminated by a single `\n` (0x0A)
- Maximum frame size: 4,096 bytes (enforced by both sides)
- Character encoding: UTF-8
- No binary framing, no length prefix, no CRC (UART parity provides basic error detection)
- Both sides discard frames that exceed 4,096 bytes and log a warning

### Message Types

#### `hello` — Brain initiates handshake

Sent by the brain immediately after DET goes high and debounce settles.

```json
{"type":"hello","ver":"1"}
```

| Field | Type   | Description                                |
|-------|--------|--------------------------------------------|
| type  | string | Always `"hello"`                           |
| ver   | string | Protocol version. Brain supports `"1"`.    |

#### `ack` — Peripheral acknowledges hello

Sent by the peripheral in response to `hello`. Announces identity and tool count.

```json
{"type":"ack","name":"robotic_arm","display_name":"6-DOF Robotic Arm","ver":"1","tools":3,"skills":1}
```

| Field        | Type   | Description                                          |
|--------------|--------|------------------------------------------------------|
| type         | string | Always `"ack"`                                       |
| name         | string | Machine name (snake_case, used in file paths)        |
| display_name | string | Human-readable name (shown in logs)                  |
| ver          | string | Peripheral firmware version (semver)                 |
| tools        | int    | Number of tools in the manifest                      |
| skills       | int    | Number of skill files available                      |

#### `manifest_req` — Brain requests the full manifest

```json
{"type":"manifest_req"}
```

#### `manifest` — Peripheral sends full manifest

```json
{
  "type": "manifest",
  "device": {
    "name": "robotic_arm",
    "display_name": "6-DOF Robotic Arm",
    "version": "1.0.0",
    "description": "A 6-degree-of-freedom robotic arm with gripper."
  },
  "tools": [
    {
      "name": "arm_move",
      "description": "Move the arm end-effector to an absolute XYZ position in mm.",
      "input_schema": {
        "type": "object",
        "properties": {
          "x": {"type": "number", "description": "X position in mm"},
          "y": {"type": "number", "description": "Y position in mm"},
          "z": {"type": "number", "description": "Z position in mm"}
        },
        "required": ["x", "y", "z"]
      }
    },
    {
      "name": "arm_grip",
      "description": "Open or close the gripper. width=0 closes fully, width=100 opens fully.",
      "input_schema": {
        "type": "object",
        "properties": {
          "width": {"type": "integer", "description": "Gripper width 0–100"}
        },
        "required": ["width"]
      }
    },
    {
      "name": "arm_home",
      "description": "Return the arm to its home/rest position.",
      "input_schema": {
        "type": "object",
        "properties": {},
        "required": []
      }
    }
  ],
  "skills": ["robotic_arm"]
}
```

The manifest must fit in a single UART frame (4,096 bytes). For peripherals with many tools, keep descriptions concise.

#### `skill_req` — Brain requests a skill file by index

```json
{"type":"skill_req","index":0}
```

| Field | Type | Description                                |
|-------|------|--------------------------------------------|
| index | int  | Zero-based index into the `skills` array in the manifest |

#### `skill` — Peripheral sends skill file content

```json
{"type":"skill","name":"robotic_arm","content":"# Robotic Arm\n\nThis peripheral provides a 6-DOF robotic arm.\n\n## Usage\n- Use `arm_move` to position..."}
```

| Field   | Type   | Description                                                   |
|---------|--------|---------------------------------------------------------------|
| name    | string | Skill name (matches entry in manifest `skills` array)         |
| content | string | Full Markdown content of the skill file (newlines as `\n`)    |

The content is written by the brain to `/spiffs/skills/peripheral_<name>.md`.

#### `ready` — Brain signals handshake complete

Sent after all skills have been received and written to SPIFFS.

```json
{"type":"ready"}
```

#### `ready_ack` — Peripheral acknowledges ready

```json
{"type":"ready_ack"}
```

After `ready_ack`, both sides transition to operational mode. The brain may now send `tool_call` frames.

#### `tool_call` — Brain requests tool execution

```json
{"type":"tool_call","id":"abc123","tool":"arm_move","input":{"x":100,"y":50,"z":200}}
```

| Field | Type   | Description                                                        |
|-------|--------|--------------------------------------------------------------------|
| id    | string | Unique call ID generated by the brain (8-char hex)                 |
| tool  | string | Tool name (must match a tool declared in the manifest)             |
| input | object | Tool input parameters matching the tool's `input_schema`           |

The brain generates the `id` as a random 8-character hex string. Only one `tool_call` is outstanding at a time — the brain waits for `tool_result` before sending the next call.

#### `tool_result` — Peripheral returns execution result (success)

```json
{"type":"tool_result","id":"abc123","ok":true,"output":"Moved to (100, 50, 200) in 1.2s"}
```

#### `tool_result` — Peripheral returns execution result (failure)

```json
{"type":"tool_result","id":"abc123","ok":false,"error":"Joint angle out of range: z=200 exceeds max 180mm"}
```

| Field  | Type   | Description                                              |
|--------|--------|----------------------------------------------------------|
| id     | string | Echo of the `id` from the `tool_call`                    |
| ok     | bool   | `true` on success, `false` on failure                    |
| output | string | Human-readable result (present when `ok=true`)           |
| error  | string | Error description (present when `ok=false`)              |

### Error Codes

| Situation                            | Brain behavior                                             |
|--------------------------------------|------------------------------------------------------------|
| No `ack` within 5 s of `hello`      | Log warning, return to IDLE (peripheral not supported)     |
| No `manifest` within 5 s of `manifest_req` | Log warning, return to IDLE                        |
| No `skill` within 5 s of `skill_req` | Log warning, return to IDLE                              |
| No `tool_result` within 10 s         | Return error string to agent: `"Peripheral timeout"`      |
| `tool_result` with `ok=false`        | Return `error` string to agent as tool result content     |
| Frame exceeds 4,096 bytes            | Log warning, discard frame, continue                       |
| Invalid JSON frame                   | Log warning, discard frame, continue                       |
| DET goes low during tool call        | Cancel pending call, unregister tools, return to IDLE     |

### Handshake Sequence Diagram

```
Brain (ESP32-S3)                       Peripheral (MCU)
       │                                      │
       │  [DET pin goes high]                 │
       │  [50ms debounce settles]             │
       │                                      │
       │──── {"type":"hello","ver":"1"} ─────▶│
       │                                      │  [verify protocol version]
       │◀─── {"type":"ack","name":"robotic_arm","tools":3,"skills":1} ──│
       │                                      │
       │──── {"type":"manifest_req"} ────────▶│
       │                                      │
       │◀─── {"type":"manifest",...} ─────────│
       │                                      │
       │  [parse manifest, create tool stubs] │
       │                                      │
       │  [for each skill index 0..N-1]       │
       │──── {"type":"skill_req","index":0} ─▶│
       │◀─── {"type":"skill","name":"robotic_arm","content":"..."} ─────│
       │  [write /spiffs/skills/peripheral_robotic_arm.md]              │
       │                                      │
       │──── {"type":"ready"} ───────────────▶│
       │◀─── {"type":"ready_ack"} ────────────│
       │                                      │
       │  [register N tools in tool_registry] │
       │  [rebuild tools JSON]                │
       │  [peripheral_manager state = READY]  │
       │                                      │
       │  ... operational: tool calls ...     │
       │                                      │
       │──── {"type":"tool_call","id":"a1b2","tool":"arm_move",         │
       │      "input":{"x":100,"y":50,"z":200}} ────────────────────────▶│
       │                                      │  [execute arm_move()]
       │◀─── {"type":"tool_result","id":"a1b2","ok":true,              │
       │      "output":"Moved to (100,50,200) in 1.2s"} ───────────────│
       │                                      │
       │  [DET pin goes low]                  │
       │  [50ms debounce settles]             │
       │  [unregister peripheral tools]       │
       │  [delete skill files from SPIFFS]    │
       │  [peripheral_manager state = IDLE]   │
```

---

## 5. Manifest Format Specification

The manifest is the complete capability declaration of a peripheral. It is sent as the payload of a `manifest` frame during handshake.

### JSON Schema

```json
{
  "type": "manifest",
  "device": {
    "name":         "<string: snake_case identifier>",
    "display_name": "<string: human-readable name>",
    "version":      "<string: semver e.g. 1.0.0>",
    "description":  "<string: one-line description>"
  },
  "tools": [
    {
      "name":        "<string: snake_case tool name>",
      "description": "<string: what the tool does, for the LLM>",
      "input_schema": {
        "type": "object",
        "properties": {
          "<param_name>": {
            "type":        "<string|number|integer|boolean>",
            "description": "<string: parameter description>"
          }
        },
        "required": ["<param_name>", ...]
      }
    }
  ],
  "skills": ["<skill_name>", ...]
}
```

### Field Reference

| Field                           | Required | Description                                                        |
|---------------------------------|----------|--------------------------------------------------------------------|
| `device.name`                   | Yes      | Machine identifier. Used in file paths. snake_case. Max 63 chars.  |
| `device.display_name`           | Yes      | Human-readable name shown in logs.                                 |
| `device.version`                | Yes      | Firmware version in semver format (`MAJOR.MINOR.PATCH`).           |
| `device.description`            | Yes      | One-line description of what the peripheral does.                  |
| `tools`                         | Yes      | Array of tool definitions. May be empty (`[]`).                    |
| `tools[].name`                  | Yes      | Tool name sent to the LLM. snake_case. Prefix with device name.    |
| `tools[].description`           | Yes      | Natural language description of the tool for the LLM.              |
| `tools[].input_schema`          | Yes      | JSON Schema object describing input parameters.                    |
| `tools[].input_schema.type`     | Yes      | Always `"object"`.                                                 |
| `tools[].input_schema.properties` | Yes   | Object mapping parameter names to type descriptors.               |
| `tools[].input_schema.required` | Yes      | Array of required parameter names (may be empty `[]`).            |
| `skills`                        | Yes      | Array of skill names (one per skill file). May be empty (`[]`).   |

### Full Example: Robotic Arm

```json
{
  "type": "manifest",
  "device": {
    "name": "robotic_arm",
    "display_name": "6-DOF Robotic Arm",
    "version": "1.0.0",
    "description": "A 6-degree-of-freedom desktop robotic arm with servo control and pneumatic gripper."
  },
  "tools": [
    {
      "name": "arm_move",
      "description": "Move the arm end-effector to an absolute XYZ position in millimeters relative to the arm base. Returns confirmation with actual travel time.",
      "input_schema": {
        "type": "object",
        "properties": {
          "x": {"type": "number", "description": "X axis position in mm. Range: -200 to 200."},
          "y": {"type": "number", "description": "Y axis position in mm. Range: -200 to 200."},
          "z": {"type": "number", "description": "Z axis height in mm. Range: 0 to 300."},
          "speed": {"type": "integer", "description": "Movement speed 1-100 (default: 50)."}
        },
        "required": ["x", "y", "z"]
      }
    },
    {
      "name": "arm_grip",
      "description": "Control the pneumatic gripper. width=0 closes fully (max grip force), width=100 opens fully.",
      "input_schema": {
        "type": "object",
        "properties": {
          "width": {"type": "integer", "description": "Gripper opening width 0-100."},
          "force": {"type": "integer", "description": "Grip force 1-100 (default: 80). Only effective when closing."}
        },
        "required": ["width"]
      }
    },
    {
      "name": "arm_home",
      "description": "Return the arm to its safe home/rest position. All joints move to zero angles. Use before powering down or when lost.",
      "input_schema": {
        "type": "object",
        "properties": {},
        "required": []
      }
    }
  ],
  "skills": ["robotic_arm"]
}
```

### Tool Naming Conventions

- Use `snake_case` for all tool names
- Prefix tools with the device name: `arm_move`, `sensor_read`, `display_show`
- Keep names under 32 characters
- Avoid generic names like `move` or `read` that may conflict with other tools

---

## 6. Data Flow Architecture

```
[Physical connection]
  Peripheral firmware powers up, pulls DET pin high via 10kΩ resistor

[GPIO detect]
  peripheral_detector ISR fires on GPIO 21 rising edge
  50ms debounce timer starts
  After 50ms stable-high: fires connected callback

[peripheral_manager receives callback]
  State: IDLE → HANDSHAKING
  Calls pdp_handshake():
    uart_write: {"type":"hello","ver":"1"}
    uart_read:  {"type":"ack","name":"robotic_arm",...}
  Calls pdp_request_manifest():
    uart_write: {"type":"manifest_req"}
    uart_read:  {"type":"manifest",...}
  Saves manifest to /spiffs/peripheral/manifest.json
  For each skill:
    uart_write: {"type":"skill_req","index":N}
    uart_read:  {"type":"skill","content":"..."}
    Write content to /spiffs/skills/peripheral_<name>.md
  uart_write: {"type":"ready"}
  uart_read:  {"type":"ready_ack"}
  State: HANDSHAKING → READY

[tool registration]
  For each tool in manifest:
    tool_peripheral_create_stub(name, description, schema)
    tool_registry_register_dynamic(&stub)
  tool_registry_rebuild_json()
  Skill summaries auto-included in next context_builder call

[agent loop receives user message]
  context_builder includes /spiffs/skills/peripheral_robotic_arm.md in system prompt
  tools JSON now includes arm_move, arm_grip, arm_home
  LLM receives updated context + tools

[tool_use: LLM calls arm_move]
  agent_loop calls tool_registry_execute("arm_move", input_json, ...)
  tool_registry dispatches to peripheral stub's execute()
  peripheral_manager_tool_call("arm_move", input_json, output, size):
    Generates id: "a1b2c3d4"
    uart_write: {"type":"tool_call","id":"a1b2c3d4","tool":"arm_move","input":{...}}
    uart_read (10s timeout): {"type":"tool_result","id":"a1b2c3d4","ok":true,"output":"Moved in 1.2s"}
    Returns output string to agent

[peripheral firmware]
  Parses tool_call JSON
  Executes arm_move(x, y, z)
  Writes result: {"type":"tool_result","id":"a1b2c3d4","ok":true,"output":"Moved to (100,50,200) in 1.2s"}

[agent response]
  Tool result returned to LLM in next ReAct iteration
  LLM composes final response: "I moved the arm to position (100, 50, 200). It arrived in 1.2 seconds."
  Response pushed to outbound queue → routed to Telegram/WebSocket
```

---

## 7. SPIFFS Storage Paths

| Path                                      | Written by           | Content                                   |
|-------------------------------------------|----------------------|-------------------------------------------|
| `/spiffs/peripheral/manifest.json`        | peripheral_manager   | Current peripheral manifest (full JSON)   |
| `/spiffs/skills/peripheral_<name>.md`     | peripheral_manager   | Peripheral skill file (Markdown)          |

On peripheral disconnect, `peripheral_manager` deletes both the manifest file and all `peripheral_*` skill files from SPIFFS, then unregisters the peripheral tools from the tool registry.

On the next `context_builder` call after disconnect, the skill is no longer included in the system prompt. The agent is unaware of the peripheral on the next message.

---

## 8. Design Principles

### Layer 1 Always Independent

`peripheral_manager_init()` and `peripheral_manager_start()` failures are handled with `ESP_LOGW` and early return — never `ESP_ERROR_CHECK`. The Brain Box boots and operates fully even if:

- No peripheral is connected
- UART1 pins are floating
- Handshake times out or returns garbage
- The peripheral firmware crashes mid-session

### Zero Impact When No Peripheral Connected

The `peripheral_manager` task blocks on a FreeRTOS event group, consuming no CPU cycles when idle. The tool registry has no dynamic tools. No UART buffers are allocated until a connection is detected. The agent context builder, LLM requests, and tool execution path are completely unaffected.

### Voice Module Compile-Time Disable

Voice hardware is optional and potentially absent. Add a Kconfig option:

```kconfig
# main/Kconfig.projbuild
config MIMI_VOICE_ENABLE
    bool "Enable voice I/O (requires INMP441 mic + MAX98357A speaker)"
    default n
    help
        Enable I2S microphone capture, STT transcription,
        TTS synthesis, and speaker output. Requires:
        - INMP441 MEMS microphone on GPIO 4/5/6
        - MAX98357A I2S amplifier on GPIO 15/16/7
        - DashScope or OpenAI API key for STT/TTS
```

Voice source files are guarded with `#if CONFIG_MIMI_VOICE_ENABLE` and excluded from `CMakeLists.txt` when disabled. This avoids pulling in I2S driver code and the minimp3 library when not needed.

### Pure C / ESP-IDF Only

All peripheral and voice code is C99, using only ESP-IDF APIs. The single external library exception is **minimp3** (a public-domain single-header MP3 decoder included in `main/voice/minimp3.h`) required for TTS audio playback. No build system changes beyond adding the header to the source tree.

### PSRAM for Large Buffers

| Buffer                        | Size   | Location        |
|-------------------------------|--------|-----------------|
| Manifest JSON receive buffer  | 4 KB   | PSRAM (stack alloc is insufficient) |
| Skill file content buffer     | 4 KB   | PSRAM           |
| Voice recording buffer        | 160 KB | PSRAM           |
| TTS audio decode buffer       | 64 KB  | PSRAM           |

All allocations use `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`. The main agent loop PSRAM budget (~7.7 MB available after existing allocations) comfortably accommodates these additions.

---

## 9. mimi_config.h Constants Reference

The following constants are added to `main/mimi_config.h` for the peripheral and voice subsystems.

### Peripheral UART and GPIO

```c
/* UART port and GPIO pins for peripheral communication */
#define MIMI_PERIPH_UART_PORT        UART_NUM_1
#define MIMI_PERIPH_UART_BAUD        115200
#define MIMI_PERIPH_UART_TX_PIN      17
#define MIMI_PERIPH_UART_RX_PIN      18
#define MIMI_PERIPH_DETECT_PIN       21    /* DET: low=disconnected, high=connected */

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
```

### Voice I2S Ports

```c
/* I2S port assignment */
#define MIMI_VOICE_I2S_MIC_PORT      I2S_NUM_0
#define MIMI_VOICE_I2S_SPK_PORT      I2S_NUM_1
```

### Microphone GPIO (INMP441)

```c
#define MIMI_VOICE_MIC_WS_PIN        4
#define MIMI_VOICE_MIC_SCK_PIN       5
#define MIMI_VOICE_MIC_SD_PIN        6
```

### Speaker GPIO (MAX98357A)

```c
#define MIMI_VOICE_SPK_BCLK_PIN      15
#define MIMI_VOICE_SPK_WS_PIN        16
#define MIMI_VOICE_SPK_DIN_PIN       7
```

### Button and Recording Parameters

```c
/* Button pin for push-to-talk (BOOT key = GPIO0, or external) */
#define MIMI_VOICE_BTN_PIN           0

/* Recording parameters */
#define MIMI_VOICE_SAMPLE_RATE       16000
#define MIMI_VOICE_REC_BUF_SIZE      (160 * 1024)  /* 10s @ 16kHz 16-bit mono */
#define MIMI_VOICE_VAD_SILENCE_MS    1500
#define MIMI_VOICE_MAX_REC_MS        10000
```

### STT/TTS Providers and Models

```c
/* STT / TTS provider defaults (NVS key "voice_provider" can override) */
#define MIMI_VOICE_STT_PROVIDER      "dashscope"     /* dashscope | openai */
#define MIMI_VOICE_TTS_PROVIDER      "dashscope"
#define MIMI_VOICE_STT_MODEL         "paraformer-realtime-v2"
#define MIMI_VOICE_TTS_MODEL         "cosyvoice-v1"
#define MIMI_VOICE_TTS_VOICE_ID      "longxiaochun"
```

### Voice Task Stacks

```c
#define MIMI_VOICE_INPUT_STACK       (8 * 1024)
#define MIMI_VOICE_OUTPUT_STACK      (8 * 1024)
#define MIMI_VOICE_INPUT_PRIO        4
#define MIMI_VOICE_OUTPUT_PRIO       4
```

### Voice Channel and NVS

```c
/* Voice channel name (used in mimi_msg_t.channel) */
#define MIMI_CHAN_VOICE               "voice"

/* NVS namespace for voice config */
#define MIMI_NVS_VOICE               "voice_config"
#define MIMI_NVS_KEY_VOICE_PROVIDER  "provider"
#define MIMI_NVS_KEY_VOICE_KEY       "api_key"
#define MIMI_NVS_KEY_VOICE_MODEL     "model"
```

### Updated FreeRTOS Task Layout

After adding peripheral and voice subsystems:

| Task                  | Core | Priority | Stack   | Description                              |
|-----------------------|------|----------|---------|------------------------------------------|
| `tg_poll`             | 0    | 5        | 12 KB   | Telegram long polling                    |
| `agent_loop`          | 1    | 6        | 24 KB   | ReAct loop + LLM API call                |
| `outbound`            | 0    | 5        | 12 KB   | Route responses to channels              |
| `cron`                | any  | 4        | 4 KB    | Polls due cron jobs every 60s            |
| `serial_cli`          | 0    | 3        | 4 KB    | USB serial console REPL                  |
| `peripheral_manager`  | 0    | 4        | 6 KB    | Peripheral lifecycle, PDP handshake      |
| `voice_input`         | 0    | 4        | 8 KB    | I2S mic capture + STT (if enabled)       |
| `voice_output`        | 0    | 4        | 8 KB    | TTS + I2S speaker playback (if enabled)  |
| httpd (internal)      | 0    | 5        | —       | WebSocket server                         |
| wifi_event (IDF)      | 0    | 8        | —       | WiFi event handling                      |
| heartbeat (timer)     | —    | —        | —       | FreeRTOS timer, fires every 30 min       |
