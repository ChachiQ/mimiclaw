# MimiClaw Peripheral SDK

> Developer guide for building hardware peripherals that connect to a MimiClaw Brain Box.
> Read this if you are writing firmware for a device that will plug into MimiClaw via the 6-pin magnetic connector.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Start (Arduino)](#2-quick-start-arduino)
3. [Manifest Writing Guide](#3-manifest-writing-guide)
4. [Skill File Writing Guide](#4-skill-file-writing-guide)
5. [Tool Implementation Guide](#5-tool-implementation-guide)
6. [Advanced: Multi-Tool Coordination](#6-advanced-multi-tool-coordination)
7. [Testing with Python](#7-testing-with-python)
8. [Supported MCU Platforms](#8-supported-mcu-platforms)

---

## 1. Overview

### What is PDP?

PDP (Peripheral Device Protocol) is the communication protocol between a MimiClaw Brain Box and an attached peripheral MCU. It runs over a 3.3 V UART link at 115,200 baud using newline-delimited JSON frames.

When your peripheral connects to the Brain Box via the 6-pin magnetic connector, the brain automatically:

1. Detects the connection via the DET pin going high
2. Sends a `hello` handshake
3. Downloads your manifest (tool definitions)
4. Downloads your skill files (Markdown documentation)
5. Registers your tools into its AI agent

From that moment, the Claude AI running on the Brain Box can call your peripheral's tools in real-time, in response to natural language requests from Telegram or WebSocket clients.

### What Your Peripheral Must Implement

| Requirement                | Description                                                                  |
|----------------------------|------------------------------------------------------------------------------|
| DET pin high when powered  | Pull GPIO 21 on the brain high (via 10 kΩ to 3.3 V) while your MCU is ready |
| UART 115200-8N1            | Serial communication at 115,200 baud, 8 data bits, no parity, 1 stop bit     |
| JSON frame parser          | Parse newline-terminated JSON objects (max 4,096 bytes per frame)            |
| PDP handshake              | Respond to `hello`, `manifest_req`, and `skill_req` messages                 |
| Tool execution             | Parse `tool_call`, execute the operation, return `tool_result`               |

### What You Get

In exchange for implementing PDP, the MimiClaw agent:

- Presents your tools to Claude with full descriptions and JSON schemas
- Injects your skill documentation into the AI's context window
- Routes natural language commands to your hardware automatically
- Handles all LLM API calls, Telegram messaging, and persistent memory

Your firmware handles only the hardware execution. The AI reasoning is handled by the brain.

---

## 2. Quick Start (Arduino)

The following sketch implements the minimal PDP protocol for an Arduino with Serial1 available (Mega, Leonardo, ESP32, RP2040 in Arduino mode). It implements a simple temperature sensor peripheral with one tool: `sensor_read`.

### Required Library

Install **ArduinoJson** v6 via the Arduino Library Manager.

### Complete Minimal PDP Sketch

```cpp
/*
 * MimiClaw PDP Peripheral — Minimal Temperature Sensor Example
 *
 * Hardware:
 *   - Any Arduino with Serial1 (hardware UART separate from USB)
 *   - Connect Brain TX (GPIO17) -> Arduino Serial1 RX
 *   - Connect Brain RX (GPIO18) -> Arduino Serial1 TX
 *   - Connect Brain DET (GPIO21) -> Arduino GPIO via 10k pull-up to 3.3V
 *   - Connect Brain GND -> Arduino GND
 *   - Connect Brain VCC (3.3V) -> Arduino 3.3V rail
 *
 * Libraries:
 *   - ArduinoJson v6 (https://arduinojson.org/)
 */

#include <ArduinoJson.h>

/* ------------------------------------------------------------------ */
/* Manifest: describe your device and tools                           */
/* ------------------------------------------------------------------ */
const char MANIFEST[] = R"({
  "type": "manifest",
  "device": {
    "name": "temp_sensor",
    "display_name": "Temperature Sensor",
    "version": "1.0.0",
    "description": "Reads ambient temperature from a connected NTC thermistor."
  },
  "tools": [
    {
      "name": "sensor_read",
      "description": "Read the current ambient temperature from the sensor. Returns temperature in Celsius.",
      "input_schema": {
        "type": "object",
        "properties": {},
        "required": []
      }
    }
  ],
  "skills": ["temp_sensor"]
})";

/* ------------------------------------------------------------------ */
/* Skill file content: Markdown documentation for the AI              */
/* ------------------------------------------------------------------ */
const char SKILL_CONTENT[] =
    "# Temperature Sensor\n\n"
    "This peripheral provides ambient temperature readings.\n\n"
    "## Available Tool\n\n"
    "### sensor_read\n\n"
    "Reads current temperature. No parameters required.\n\n"
    "**Example usage:** \"What is the current temperature?\"\n\n"
    "The tool returns temperature in degrees Celsius as a float.\n\n"
    "## Notes\n\n"
    "- Sensor accuracy: ±0.5°C\n"
    "- Range: -10°C to 85°C\n"
    "- Reading takes approximately 100ms\n";

/* ------------------------------------------------------------------ */
/* Hardware: read temperature from NTC thermistor on A0               */
/* ------------------------------------------------------------------ */
float read_temperature_celsius() {
    /* Simple NTC thermistor reading on A0 */
    /* Replace with your actual sensor reading code */
    int raw = analogRead(A0);
    float voltage = raw * (3.3f / 1023.0f);
    float resistance = (3.3f - voltage) / (voltage / 10000.0f);  /* 10k series resistor */

    /* Steinhart-Hart approximation for common 10k NTC */
    float log_r = log(resistance / 10000.0f);
    float temp_k = 1.0f / (0.001129148f + (0.000234125f * log_r) + (0.0000000876741f * log_r * log_r * log_r));
    return temp_k - 273.15f;
}

/* ------------------------------------------------------------------ */
/* PDP protocol state machine                                         */
/* ------------------------------------------------------------------ */
#define MAX_LINE 4096
char line_buf[MAX_LINE];
int  line_len = 0;

/* Send a JSON line over Serial1 */
void pdp_send(const char *json) {
    Serial1.print(json);
    Serial1.print('\n');
    Serial1.flush();
}

/* Read one newline-terminated JSON line into line_buf.
   Returns true when a complete line is available. */
bool pdp_read_line() {
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
            line_buf[line_len] = '\0';
            line_len = 0;
            return true;
        }
        if (line_len < MAX_LINE - 1) {
            line_buf[line_len++] = c;
        }
    }
    return false;
}

/* Handle an incoming PDP frame.
   Returns true if the handshake is complete and we are in operational mode. */
bool operational = false;

void handle_frame(const char *json) {
    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        /* Ignore malformed frames */
        return;
    }

    const char *type = doc["type"];
    if (!type) return;

    /* ---- Handshake phase ---- */

    if (strcmp(type, "hello") == 0) {
        /* Brain is initiating handshake. Verify protocol version. */
        const char *ver = doc["ver"];
        if (!ver || strcmp(ver, "1") != 0) {
            /* Unsupported protocol version — do not respond */
            return;
        }
        /* Respond with our identity */
        pdp_send(
            "{\"type\":\"ack\","
            "\"name\":\"temp_sensor\","
            "\"display_name\":\"Temperature Sensor\","
            "\"ver\":\"1\","
            "\"tools\":1,"
            "\"skills\":1}"
        );
        return;
    }

    if (strcmp(type, "manifest_req") == 0) {
        /* Brain wants the full manifest */
        pdp_send(MANIFEST);
        return;
    }

    if (strcmp(type, "skill_req") == 0) {
        /* Brain wants a skill file by index */
        int index = doc["index"] | -1;
        if (index == 0) {
            /* Build the skill frame: embed skill content as a JSON string value */
            StaticJsonDocument<2048> skill_doc;
            skill_doc["type"] = "skill";
            skill_doc["name"] = "temp_sensor";
            skill_doc["content"] = SKILL_CONTENT;

            char skill_frame[2048];
            serializeJson(skill_doc, skill_frame, sizeof(skill_frame));
            pdp_send(skill_frame);
        }
        /* Ignore out-of-range indices */
        return;
    }

    if (strcmp(type, "ready") == 0) {
        /* Brain has finished loading all skills. Acknowledge and go operational. */
        pdp_send("{\"type\":\"ready_ack\"}");
        operational = true;
        return;
    }

    /* ---- Operational phase ---- */

    if (strcmp(type, "tool_call") == 0 && operational) {
        const char *id   = doc["id"];
        const char *tool = doc["tool"];
        if (!id || !tool) return;

        StaticJsonDocument<512> result_doc;
        result_doc["type"] = "tool_result";
        result_doc["id"]   = id;  /* Echo the call ID */

        if (strcmp(tool, "sensor_read") == 0) {
            float temp_c = read_temperature_celsius();

            char output[64];
            snprintf(output, sizeof(output), "Temperature: %.1f°C", temp_c);

            result_doc["ok"]     = true;
            result_doc["output"] = output;
        } else {
            /* Unknown tool — this should not happen if manifest is correct */
            result_doc["ok"]    = false;
            result_doc["error"] = "Unknown tool";
        }

        char result_frame[512];
        serializeJson(result_doc, result_frame, sizeof(result_frame));
        pdp_send(result_frame);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Arduino setup / loop                                               */
/* ------------------------------------------------------------------ */
void setup() {
    /* Debug output on USB Serial (optional) */
    Serial.begin(115200);
    Serial.println("MimiClaw PDP peripheral starting...");

    /* PDP communication on Serial1 */
    Serial1.begin(115200);

    /* DET pin: pull high to signal presence to brain.
       Connect this GPIO to brain's GPIO21 through a 10k resistor to 3.3V.
       When this MCU is powered and ready, the DET line stays high. */
    /* No code needed here — the 10k pull-up resistor handles it passively. */

    Serial.println("Ready. Waiting for brain hello...");
}

void loop() {
    if (pdp_read_line()) {
        handle_frame(line_buf);
    }

    /* Add your own background work here (sensor polling, etc.)
       Keep it fast — do not block for more than a few ms in loop() */
}
```

### Testing the Sketch

1. Upload the sketch to your Arduino
2. Open the Arduino Serial Monitor at 115200 baud (USB Serial)
3. On the Brain Box serial CLI, run: `heap_info` to verify the brain is running
4. Connect the peripheral hardware to the 6-pin connector
5. Watch the brain logs: you should see the handshake sequence, then `sensor_read` tool appearing

You can test the tool directly from the brain CLI without AI:

```
mimi> tool_exec sensor_read '{}'
Temperature: 23.4°C
```

---

## 3. Manifest Writing Guide

The manifest is the complete capability declaration your peripheral sends during handshake. The Brain Box reads it once per connection; accuracy here determines what the AI can do with your hardware.

### Device Section

```json
"device": {
    "name":         "my_device",
    "display_name": "My Device",
    "version":      "1.0.0",
    "description":  "One sentence about what this device does."
}
```

| Field          | Rules                                                                 |
|----------------|-----------------------------------------------------------------------|
| `name`         | snake_case only. Letters, digits, underscore. Max 63 chars. Used as a filename prefix in SPIFFS paths. Must be unique if you have multiple peripheral types. |
| `display_name` | Human-readable. Shown in brain logs. Max 127 chars.                   |
| `version`      | Semantic versioning: `MAJOR.MINOR.PATCH`. Increment MINOR when you add tools, MAJOR when you break compatibility. |
| `description`  | One sentence, plain English. Helps the AI understand when to use this peripheral. |

### Tools Array

Each entry in `tools` defines one callable operation:

```json
{
    "name": "arm_move",
    "description": "Move the arm to XYZ. Returns travel time.",
    "input_schema": {
        "type": "object",
        "properties": {
            "x": {"type": "number", "description": "X in mm. Range -200 to 200."},
            "y": {"type": "number", "description": "Y in mm. Range -200 to 200."},
            "z": {"type": "number", "description": "Z height in mm. Range 0 to 300."}
        },
        "required": ["x", "y", "z"]
    }
}
```

#### Tool Name Rules

- snake_case only (lowercase letters, digits, underscore)
- Prefix with your device name: `arm_move` not `move`
- Max 63 characters
- Must be globally unique — do not use names already taken by brain built-ins: `web_search`, `get_current_time`, `read_file`, `write_file`, `edit_file`, `list_dir`, `cron_add`, `cron_list`, `cron_remove`

#### Description Rules

The description is what Claude reads to decide when to call your tool. Write it as a clear imperative sentence. Include:

- What the tool does (action verb first)
- What units parameters use (mm, degrees, percent)
- What the return value describes
- Any important constraints

Bad: `"Moves arm."`
Good: `"Move the arm end-effector to an absolute XYZ position in millimeters. Returns the actual travel time in seconds."`

#### Input Schema: Supported Types

The brain sends your `input_schema` directly to Claude as a JSON Schema. Supported types in parameter properties:

| Type      | JSON Schema              | Arduino type | Use for                          |
|-----------|--------------------------|--------------|----------------------------------|
| `string`  | `"type":"string"`        | `const char*`| Text, enum values, mode names    |
| `number`  | `"type":"number"`        | `float`      | Decimals: position, temperature  |
| `integer` | `"type":"integer"`       | `int`        | Counts, indices, percentages     |
| `boolean` | `"type":"boolean"`       | `bool`       | On/off flags                     |

Add `"description"` to every parameter — Claude reads these to understand what to pass.

Add a `"minimum"` and `"maximum"` hint for numeric types to help Claude stay in range:

```json
"x": {
    "type": "number",
    "description": "X position in mm.",
    "minimum": -200,
    "maximum": 200
}
```

#### Required vs Optional Parameters

List all mandatory parameters in `"required"`. Parameters not in `required` are optional — your firmware should handle them being absent and apply defaults.

```json
"input_schema": {
    "type": "object",
    "properties": {
        "width":  {"type": "integer", "description": "Gripper width 0-100. Required."},
        "force":  {"type": "integer", "description": "Grip force 1-100. Default: 80."}
    },
    "required": ["width"]
}
```

In firmware: `int force = doc["input"]["force"] | 80;` (default 80 if absent).

### Skills Array

```json
"skills": ["robotic_arm", "gripper_safety"]
```

Each string must exactly match the `name` field you return in the corresponding `skill` message. The brain requests them in order by index (0, 1, ...).

For simple peripherals: one skill file is sufficient. For complex peripherals with multiple subsystems (arm + gripper + camera), split into multiple skill files.

---

## 4. Skill File Writing Guide

Skill files are Markdown documents the brain injects into Claude's system prompt. Claude reads them at the start of every conversation. Good skill documentation makes the difference between an AI that uses your hardware confidently and one that stumbles.

### Format

Plain Markdown. The brain treats the entire file as a block of text injected into the system prompt. No special syntax is required.

Keep skill files under 4,000 characters. Very long skill files consume context window and slow down the AI.

### What to Include

**1. Device description (1-2 sentences)**

What the device is, what it can do, and when the user might want to use it.

**2. Tool usage examples**

For each tool: a natural language example and the expected tool call. This teaches Claude the mapping from intent to action.

**3. Combination patterns**

If tools are meant to be used in sequence (home → move → grip), document the pattern explicitly. Claude will replicate it.

**4. Safety notes**

Any constraints the AI must respect: maximum travel limits, required sequences, dangerous operations. Claude takes safety notes seriously.

**5. Unit conventions**

State units clearly. "Millimeters" vs "centimeters" ambiguity causes incorrect calls.

### Example: Robotic Arm Skill File

```markdown
# Robotic Arm

A 6-DOF desktop robotic arm with servo control and pneumatic gripper. Use it to
pick up and place objects within a 200mm radius of the arm base.

## Tools

### arm_move
Moves the arm end-effector to an absolute XYZ position in millimeters.

- X and Y range: -200mm to 200mm (relative to arm base center)
- Z range: 0mm (table level) to 300mm (maximum height)
- Returns confirmation with actual travel time in seconds

**Example:** "Move the arm to position 100, 50, 150"
→ arm_move(x=100, y=50, z=150)

### arm_grip
Controls the pneumatic gripper opening.

- width=0: fully closed (maximum grip force)
- width=100: fully open
- Close slowly for delicate objects: use width=20 to 0 in steps

**Example:** "Pick up the block" → arm_move to block position, then arm_grip(width=0)
**Example:** "Release the object" → arm_grip(width=100)

### arm_home
Returns the arm to its safe rest position. All joints return to zero angle.

Always call arm_home before powering down or when the arm is in an unknown position.

**Example:** "Park the arm" → arm_home()

## Pickup Sequence

To pick up an object at position (x, y):
1. arm_home() — ensure known starting state
2. arm_grip(width=100) — open gripper
3. arm_move(x=x, y=y, z=50) — position above object
4. arm_move(x=x, y=y, z=0) — lower to object
5. arm_grip(width=0) — close gripper
6. arm_move(x=x, y=y, z=100) — lift object

## Safety Notes

- Never command Z below 0. The table surface is at Z=0.
- Do not exceed speed=80 when carrying fragile objects.
- If the arm stops responding, call arm_home() to recover.
- Maximum payload: 200 grams.
```

---

## 5. Tool Implementation Guide

### Parsing tool_call

Every `tool_call` frame has this structure:

```json
{"type":"tool_call","id":"a1b2c3d4","tool":"arm_move","input":{"x":100,"y":50,"z":200}}
```

The three fields your firmware must read:

| Field  | How to read (ArduinoJson)           | Notes                         |
|--------|--------------------------------------|-------------------------------|
| `id`   | `const char *id = doc["id"];`       | Echo this back in tool_result |
| `tool` | `const char *tool = doc["tool"];`   | Dispatch to your handler      |
| `input`| `JsonObject input = doc["input"];`  | Extract parameters from here  |

### Reading Parameters

```cpp
JsonObject input = doc["input"];

/* Required parameter — no default needed */
float x = input["x"];
float y = input["y"];
float z = input["z"];

/* Optional parameter with default */
int speed = input["speed"] | 50;    /* default: 50 */

/* Boolean parameter */
bool enabled = input["enabled"] | true;

/* String parameter */
const char *mode = input["mode"] | "normal";
```

Always apply range clamping on numeric inputs before passing to hardware:

```cpp
z = constrain(z, 0.0f, 300.0f);  /* enforce Z limits */
speed = constrain(speed, 1, 100);
```

### Constructing tool_result

**On success:**

```cpp
StaticJsonDocument<512> result;
result["type"]   = "tool_result";
result["id"]     = id;          /* echo the call ID */
result["ok"]     = true;
result["output"] = "Moved to (100, 50, 200) in 1.2s";

char frame[512];
serializeJson(result, frame, sizeof(frame));
pdp_send(frame);
```

**On failure:**

```cpp
StaticJsonDocument<512> result;
result["type"]  = "tool_result";
result["id"]    = id;
result["ok"]    = false;
result["error"] = "Z=350 exceeds maximum height of 300mm";

char frame[512];
serializeJson(result, frame, sizeof(frame));
pdp_send(frame);
```

### The `output` String

The `output` string is returned directly to Claude as the tool result. Write it as a human-readable sentence the AI can incorporate into its response:

Good: `"Moved to (100, 50, 200) in 1.2 seconds. Gripper is open."`
Bad: `"OK"` — too terse, Claude cannot form a useful response
Bad: `{"x":100,"y":50}` — raw JSON confuses Claude, it expects natural language

### Generating the Call ID

You do not generate the call ID — the brain generates it. You simply echo back whatever `id` value you received in the `tool_call`. This allows the brain to match responses to requests.

```cpp
const char *id = doc["id"];
/* ... do work ... */
result["id"] = id;  /* echo, do not modify */
```

### Error Handling

Return a `tool_result` with `ok=false` for any of these situations:

- Parameter out of valid range
- Hardware fault (motor stall, sensor failure)
- Operation blocked by safety constraint
- Unknown tool name

Never leave a `tool_call` unanswered. If your MCU gets into an unknown state, send a failure result and attempt to recover:

```cpp
/* Emergency: hardware error during execution */
result["ok"]    = false;
result["error"] = "Motor stall detected on joint 3. Attempting home.";
pdp_send(frame);
arm_home();  /* attempt recovery */
```

### Timeout Considerations

The brain waits `MIMI_PERIPH_TOOL_TIMEOUT_MS` = 10 seconds for a `tool_result`. If your operation takes longer than 10 seconds:

**Option A**: Break it into smaller operations. Instead of `arm_move_slow` taking 15s, have the AI call `arm_move` twice to intermediate positions.

**Option B**: Return a partial result immediately, then update SPIFFS via a `write_file` tool call from the AI in a follow-up. (The AI can check status with a `sensor_read` tool on the next turn.)

**Option C**: Increase `MIMI_PERIPH_TOOL_TIMEOUT_MS` in `mimi_config.h` and rebuild the brain firmware.

---

## 6. Advanced: Multi-Tool Coordination

### Sequential Tool Calls

The brain sends tool calls one at a time over the UART. Claude may call multiple tools per agent iteration, but the brain serializes them — it waits for a `tool_result` before sending the next `tool_call`.

This means your MCU handles at most one concurrent tool call. No locking is needed.

Typical multi-tool sequence from the brain:

```
brain → {"type":"tool_call","id":"aa01","tool":"arm_home",...}
brain ← {"type":"tool_result","id":"aa01","ok":true,"output":"Homed."}
brain → {"type":"tool_call","id":"bb02","tool":"arm_move","input":{"x":50,"y":0,"z":100}}
brain ← {"type":"tool_result","id":"bb02","ok":true,"output":"Moved in 0.8s"}
brain → {"type":"tool_call","id":"cc03","tool":"arm_grip","input":{"width":0}}
brain ← {"type":"tool_result","id":"cc03","ok":true,"output":"Gripper closed."}
```

### Stateful Operations

When your firmware maintains state across tool calls (e.g. tracking whether the gripper is open or closed), encode that state in your `output` strings so Claude can reason about it:

```cpp
static bool gripper_open = true;

/* In arm_grip handler: */
gripper_open = (width > 10);
snprintf(output, sizeof(output),
    "Gripper %s. Width: %d.", gripper_open ? "open" : "closed", width);
```

This lets Claude produce accurate status reports: "The gripper is currently closed and holding the block."

### Representing Error Recovery State

If a tool fails and requires operator intervention, include recovery instructions in the error string:

```cpp
result["error"] = "Servo overload on joint 2. Power cycle the arm, "
                  "then call arm_home() to resume.";
```

Claude will relay this to the user verbatim, enabling the user to take corrective action.

### Firmware Update Over PDP

If your peripheral needs a firmware update, implement an `ota_start` tool that receives a URL, downloads the firmware, and flashes it. The AI can trigger this on user request:

```
User: "Update the arm firmware to version 2.0"
Claude: (calls ota_start with URL)
Arm: Downloads and flashes. Returns: "OTA complete. Rebooting."
```

After reboot, the arm reconnects and the brain performs a fresh handshake.

---

## 7. Testing with Python

Use this Python script to simulate a peripheral on a USB-to-serial adapter for testing the brain-side PDP implementation before your real firmware is ready.

### Requirements

```bash
pip install pyserial
```

### Simulator Script

```python
#!/usr/bin/env python3
"""
MimiClaw PDP Peripheral Simulator

Simulates a temperature sensor peripheral over a serial port.
Use a USB-to-serial adapter connected to the Brain Box UART1 pins:
  - Adapter TX -> Brain GPIO18 (RX)
  - Adapter RX -> Brain GPIO17 (TX)
  - Adapter GND -> Brain GND

Usage:
    python3 pdp_simulator.py /dev/cu.usbserial-xxxx
    python3 pdp_simulator.py COM3   (Windows)
"""

import sys
import json
import time
import serial
import random

MANIFEST = {
    "type": "manifest",
    "device": {
        "name": "temp_sensor",
        "display_name": "Temperature Sensor (Simulated)",
        "version": "1.0.0",
        "description": "Simulated temperature sensor for PDP testing."
    },
    "tools": [
        {
            "name": "sensor_read",
            "description": "Read the current ambient temperature in Celsius.",
            "input_schema": {
                "type": "object",
                "properties": {},
                "required": []
            }
        },
        {
            "name": "sensor_set_alarm",
            "description": "Set a temperature alarm threshold. The sensor will print a warning when exceeded.",
            "input_schema": {
                "type": "object",
                "properties": {
                    "threshold": {
                        "type": "number",
                        "description": "Alarm temperature in Celsius."
                    }
                },
                "required": ["threshold"]
            }
        }
    ],
    "skills": ["temp_sensor"]
}

SKILL_CONTENT = """# Temperature Sensor

Simulated temperature sensor for PDP protocol testing.

## Tools

### sensor_read
Returns the current temperature reading in degrees Celsius.

**Example:** "What is the temperature?" -> sensor_read()

### sensor_set_alarm
Sets a threshold. Logs a warning when temperature exceeds the threshold.

**Example:** "Alert me if temperature exceeds 30 degrees" -> sensor_set_alarm(threshold=30)
"""

alarm_threshold = None
base_temp = 22.5


def read_temperature():
    """Simulate a temperature reading with slight drift."""
    return base_temp + random.uniform(-0.5, 0.5)


def send_frame(ser, obj):
    """Serialize obj to JSON and write as a newline-terminated frame."""
    line = json.dumps(obj) + '\n'
    ser.write(line.encode('utf-8'))
    ser.flush()
    print(f"  SENT: {line.strip()}")


def handle_frame(ser, line):
    """Handle one received PDP frame."""
    line = line.strip()
    if not line:
        return

    print(f"  RECV: {line}")

    try:
        msg = json.loads(line)
    except json.JSONDecodeError as e:
        print(f"  [warn] JSON parse error: {e}")
        return

    msg_type = msg.get("type")

    if msg_type == "hello":
        ver = msg.get("ver", "")
        if ver != "1":
            print(f"  [warn] Unsupported protocol version: {ver}")
            return
        send_frame(ser, {
            "type": "ack",
            "name": "temp_sensor",
            "display_name": "Temperature Sensor (Simulated)",
            "ver": "1",
            "tools": len(MANIFEST["tools"]),
            "skills": len(MANIFEST["skills"])
        })

    elif msg_type == "manifest_req":
        send_frame(ser, MANIFEST)

    elif msg_type == "skill_req":
        index = msg.get("index", -1)
        if index == 0:
            send_frame(ser, {
                "type": "skill",
                "name": "temp_sensor",
                "content": SKILL_CONTENT
            })
        else:
            print(f"  [warn] Unknown skill index: {index}")

    elif msg_type == "ready":
        send_frame(ser, {"type": "ready_ack"})
        print("\n[simulator] Handshake complete. Ready for tool calls.\n")

    elif msg_type == "tool_call":
        call_id = msg.get("id")
        tool    = msg.get("tool")
        inputs  = msg.get("input", {})

        if tool == "sensor_read":
            temp = read_temperature()
            send_frame(ser, {
                "type":   "tool_result",
                "id":     call_id,
                "ok":     True,
                "output": f"Temperature: {temp:.1f}°C"
            })

        elif tool == "sensor_set_alarm":
            global alarm_threshold
            threshold = inputs.get("threshold")
            if threshold is None:
                send_frame(ser, {
                    "type":  "tool_result",
                    "id":    call_id,
                    "ok":    False,
                    "error": "Missing required parameter: threshold"
                })
            else:
                alarm_threshold = float(threshold)
                send_frame(ser, {
                    "type":   "tool_result",
                    "id":     call_id,
                    "ok":     True,
                    "output": f"Alarm set at {alarm_threshold:.1f}°C"
                })
        else:
            send_frame(ser, {
                "type":  "tool_result",
                "id":    call_id,
                "ok":    False,
                "error": f"Unknown tool: {tool}"
            })

    else:
        print(f"  [warn] Unknown message type: {msg_type}")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <serial_port>")
        print(f"  Example: {sys.argv[0]} /dev/cu.usbserial-0001")
        sys.exit(1)

    port = sys.argv[1]
    print(f"[simulator] Opening {port} at 115200 baud...")

    with serial.Serial(port, 115200, timeout=0.1) as ser:
        print(f"[simulator] Port open. Waiting for brain hello...")
        print(f"[simulator] (Connect the DET wire to signal presence to the brain)")

        buffer = ""
        while True:
            # Read available bytes
            data = ser.read(256).decode('utf-8', errors='replace')
            if data:
                buffer += data
                # Process complete lines
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    handle_frame(ser, line)

            # Check alarm threshold (background simulation)
            if alarm_threshold is not None:
                temp = read_temperature()
                if temp > alarm_threshold:
                    print(f"  [alarm] Temperature {temp:.1f}°C exceeds threshold {alarm_threshold:.1f}°C")

            time.sleep(0.01)


if __name__ == "__main__":
    main()
```

### Running the Simulator

```bash
# Connect USB-to-serial adapter to brain UART1 pins
# Brain TX (GPIO17) -> Adapter RX
# Brain RX (GPIO18) -> Adapter TX
# Brain GND         -> Adapter GND

# Run simulator
python3 pdp_simulator.py /dev/cu.usbserial-0001

# Note: You also need to simulate the DET pin going high.
# Connect a 3.3V wire from your adapter's 3.3V pin (or a bench supply)
# to Brain GPIO21 through a 10k resistor to signal connection.
```

### Expected Output

When the brain detects the DET pin and connects:

```
[simulator] Opening /dev/cu.usbserial-0001 at 115200 baud...
[simulator] Port open. Waiting for brain hello...
  RECV: {"type":"hello","ver":"1"}
  SENT: {"type":"ack","name":"temp_sensor","display_name":"Temperature Sensor (Simulated)","ver":"1","tools":2,"skills":1}
  RECV: {"type":"manifest_req"}
  SENT: {"type":"manifest","device":{...},"tools":[...],"skills":["temp_sensor"]}
  RECV: {"type":"skill_req","index":0}
  SENT: {"type":"skill","name":"temp_sensor","content":"..."}
  RECV: {"type":"ready"}
  SENT: {"type":"ready_ack"}

[simulator] Handshake complete. Ready for tool calls.

  RECV: {"type":"tool_call","id":"a1b2c3d4","tool":"sensor_read","input":{}}
  SENT: {"type":"tool_result","id":"a1b2c3d4","ok":true,"output":"Temperature: 22.3°C"}
```

On the brain side, you can verify by sending a Telegram message: "What is the temperature?" Claude will call `sensor_read` and respond with the simulated reading.

---

## 8. Supported MCU Platforms

### ESP32 (Recommended for Complex Peripherals)

The best choice for peripherals that need WiFi, BLE, more than 2 UARTs, I2S, camera, or significant computation.

```cpp
// ESP32 Arduino — Serial1 is available on GPIO16/17 by default
// Reassign to safe pins if needed:
Serial1.begin(115200, SERIAL_8N1, 26, 27);  // RX=26, TX=27
```

Benefits:
- 4 hardware UARTs
- 240 MHz dual-core processor
- 520 KB SRAM + optional PSRAM
- Full ArduinoJson support
- OTA firmware updates
- Native cJSON library in ESP-IDF (if using ESP-IDF instead of Arduino)

Notes:
- Use ESP32-S3 or ESP32-C3 for newer designs (better availability)
- If using ESP-IDF instead of Arduino, replace ArduinoJson with cJSON (included in IDF)

### Arduino Uno / Nano (Simple Sensors Only)

Suitable only for simple peripherals with 1-2 tools and no complex computation.

```cpp
// Uno has only one hardware UART (used by USB).
// Use SoftwareSerial for PDP on pins 10/11:
#include <SoftwareSerial.h>
SoftwareSerial pdp_serial(10, 11);  // RX=10, TX=11
pdp_serial.begin(115200);

// Warning: SoftwareSerial at 115200 is unreliable on 8 MHz boards.
// Use 9600 baud instead and update MIMI_PERIPH_UART_BAUD in mimi_config.h.
```

Limitations:
- 2 KB SRAM — the `MAX_LINE = 4096` buffer will not fit. Reduce to 512 bytes and keep manifests small.
- SoftwareSerial is unreliable at 115200; use 9600 baud and rebuild the brain.
- ArduinoJson with `StaticJsonDocument<512>` is feasible.
- Maximum 1-2 simple tools with small input schemas.

For anything beyond a trivial sensor, use Arduino Mega or switch to ESP32.

### Arduino Mega

The best traditional Arduino for PDP peripherals:

```cpp
// Mega has Serial1 on pins 18/19, Serial2 on 16/17, Serial3 on 14/15
// Use Serial1 (hardware UART, full 115200 support):
Serial1.begin(115200);
```

Benefits over Uno:
- Hardware Serial1/2/3 (reliable 115200)
- 8 KB SRAM — enough for `MAX_LINE = 4096`
- ArduinoJson `StaticJsonDocument<4096>` fits in SRAM
- 54 digital I/O pins for complex actuator arrays

### STM32 (STM32F1/F4/G0)

High-performance choice for real-time control applications:

```cpp
// STM32 Arduino (STM32duino) — USART1 on PA9/PA10
HardwareSerial pdp_serial(PA10, PA9);  // RX, TX
pdp_serial.begin(115200);
```

Using STM32 HAL (bare-metal):

```c
/* USART2 example (PA2=TX, PA3=RX) */
USART_InitTypeDef init = {
    .BaudRate   = 115200,
    .WordLength = USART_WordLength_8b,
    .StopBits   = USART_StopBits_1,
    .Parity     = USART_Parity_No,
    .Mode       = USART_Mode_Rx | USART_Mode_Tx,
};
USART_Init(USART2, &init);
USART_Cmd(USART2, ENABLE);
```

For JSON parsing without ArduinoJson, use **jsmn** (single-header, MIT license):

```c
#include "jsmn.h"
jsmn_parser parser;
jsmntok_t tokens[64];
jsmn_init(&parser);
int count = jsmn_parse(&parser, line_buf, strlen(line_buf), tokens, 64);
/* Walk tokens to extract "type", "id", "tool", "input" fields */
```

Benefits:
- 72-480 MHz Cortex-M — fast enough for real-time motor control
- 128 KB - 2 MB flash, 20-512 KB SRAM
- Multiple hardware UARTs, DMA, hardware timers
- Suitable for servo controllers, stepper drivers, CNC applications

### RP2040 (Raspberry Pi Pico)

Excellent balance of cost, capability, and ease of use:

```cpp
// Arduino-Pico: Serial1 is on GPIO0(TX)/GPIO1(RX) by default
// Reassign for PDP:
Serial1.setTX(4);  // GPIO4 = TX to brain GPIO18
Serial1.setRX(5);  // GPIO5 = RX from brain GPIO17
Serial1.begin(115200);
```

Using Pico SDK (bare-metal):

```c
#include "pico/stdlib.h"
#include "hardware/uart.h"

uart_init(uart1, 115200);
gpio_set_function(4, GPIO_FUNC_UART);  /* TX */
gpio_set_function(5, GPIO_FUNC_UART);  /* RX */
uart_set_hw_flow(uart1, false, false);
uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
```

Benefits:
- 133 MHz dual-core ARM Cortex-M0+
- 264 KB SRAM — more than enough for 4 KB line buffer
- PIO state machines for precision PWM/timing
- Very low cost (~$1 USD)
- MicroPython option for rapid prototyping

MicroPython PDP implementation:

```python
# MicroPython on RP2040
import machine
import json

uart = machine.UART(1, baudrate=115200, tx=machine.Pin(4), rx=machine.Pin(5))

def pdp_send(obj):
    line = json.dumps(obj) + '\n'
    uart.write(line.encode())

buf = b""
while True:
    if uart.any():
        buf += uart.read(uart.any())
        if b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            msg = json.loads(line.decode())
            # ... handle msg ...
```

### Platform Comparison Summary

| Platform         | UART      | SRAM     | JSON Library  | Recommended For                          |
|------------------|-----------|----------|---------------|------------------------------------------|
| ESP32/S3         | Hardware  | 520 KB+  | ArduinoJson 6 | Complex peripherals, WiFi/BLE, cameras   |
| RP2040 (Pico)    | Hardware  | 264 KB   | ArduinoJson 6 | General peripherals, motor control       |
| STM32F4          | Hardware  | 192 KB+  | jsmn          | Real-time control, servo/stepper systems |
| Arduino Mega     | Hardware  | 8 KB     | ArduinoJson 6 | Multi-I/O, classic Arduino ecosystem     |
| STM32G0          | Hardware  | 36 KB    | jsmn          | Low-cost real-time control               |
| Arduino Uno/Nano | Software  | 2 KB     | ArduinoJson 6 | Simple sensors only (limited)            |
