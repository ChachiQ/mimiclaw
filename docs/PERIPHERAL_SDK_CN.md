# MimiClaw 外设 SDK

> 面向开发者的硬件外设构建指南，适用于通过 6 针磁性连接器接入 MimiClaw Brain Box 的设备。
> 如果您正在为将插入 MimiClaw 的设备编写固件，请阅读本文档。

---

## 目录

1. [概述](#1-概述)
2. [快速入门（Arduino）](#2-快速入门arduino)
3. [Manifest 编写指南](#3-manifest-编写指南)
4. [技能文件编写指南](#4-技能文件编写指南)
5. [工具实现指南](#5-工具实现指南)
6. [进阶：多工具协同](#6-进阶多工具协同)
7. [使用 Python 测试](#7-使用-python-测试)
8. [支持的 MCU 平台](#8-支持的-mcu-平台)

---

## 1. 概述

### 什么是 PDP？

PDP（外设设备协议，Peripheral Device Protocol）是 MimiClaw Brain Box 与所连接的外设 MCU 之间的通信协议。它基于 3.3V UART 链路，波特率为 115,200，使用换行符分隔的 JSON 帧进行通信。

当您的外设通过 6 针磁性连接器接入 Brain Box 时，Brain 会自动完成以下操作：

1. 通过 DET 引脚变高检测连接
2. 发送 `hello` 握手消息
3. 下载您的 manifest（工具定义）
4. 下载您的技能文件（Markdown 文档）
5. 将您的工具注册到其 AI 代理中

从此时起，运行在 Brain Box 上的 Claude AI 可以实时调用您外设的工具，以响应来自 Telegram 或 WebSocket 客户端的自然语言请求。

### 您的外设必须实现的功能

| 要求                       | 说明                                                                                     |
|----------------------------|------------------------------------------------------------------------------------------|
| 上电时 DET 引脚拉高        | 当 MCU 就绪时，通过 10 kΩ 电阻上拉至 3.3V，将 Brain 的 GPIO 21 拉高                    |
| UART 115200-8N1            | 115,200 波特率串行通信，8 位数据位，无奇偶校验，1 位停止位                               |
| JSON 帧解析器              | 解析以换行符结尾的 JSON 对象（每帧最大 4,096 字节）                                      |
| PDP 握手                   | 响应 `hello`、`manifest_req` 和 `skill_req` 消息                                        |
| 工具执行                   | 解析 `tool_call`，执行操作，返回 `tool_result`                                           |

### 您将获得的能力

作为实现 PDP 协议的回报，MimiClaw 代理将：

- 将您的工具连同完整描述和 JSON Schema 一起呈现给 Claude
- 将您的技能文档注入 AI 的上下文窗口
- 自动将自然语言指令路由到您的硬件
- 处理所有 LLM API 调用、Telegram 消息传递和持久化存储

您的固件只需负责硬件执行，AI 推理由 Brain 负责处理。

---

## 2. 快速入门（Arduino）

以下示例实现了适用于具有 Serial1 的 Arduino（Mega、Leonardo、ESP32、Arduino 模式下的 RP2040）的最小 PDP 协议。它实现了一个简单的温度传感器外设，包含一个工具：`sensor_read`。

### 所需库

通过 Arduino 库管理器安装 **ArduinoJson** v6。

### 完整最小 PDP 示例代码

```cpp
/*
 * MimiClaw PDP 外设 —— 最小温度传感器示例
 *
 * 硬件连接：
 *   - 任何带有 Serial1 的 Arduino（独立于 USB 的硬件 UART）
 *   - Brain TX (GPIO17) -> Arduino Serial1 RX
 *   - Brain RX (GPIO18) -> Arduino Serial1 TX
 *   - Brain DET (GPIO21) -> Arduino GPIO（通过 10k 上拉至 3.3V）
 *   - Brain GND -> Arduino GND
 *   - Brain VCC (3.3V) -> Arduino 3.3V 电源轨
 *
 * 所需库：
 *   - ArduinoJson v6 (https://arduinojson.org/)
 */

#include <ArduinoJson.h>

/* ------------------------------------------------------------------ */
/* Manifest：描述您的设备和工具                                        */
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
/* 技能文件内容：供 AI 使用的 Markdown 文档                            */
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
/* 硬件：从 A0 引脚的 NTC 热敏电阻读取温度                            */
/* ------------------------------------------------------------------ */
float read_temperature_celsius() {
    /* 从 A0 读取 NTC 热敏电阻值 */
    /* 请替换为您实际的传感器读取代码 */
    int raw = analogRead(A0);
    float voltage = raw * (3.3f / 1023.0f);
    float resistance = (3.3f - voltage) / (voltage / 10000.0f);  /* 10k 串联电阻 */

    /* 常用 10k NTC 的 Steinhart-Hart 近似公式 */
    float log_r = log(resistance / 10000.0f);
    float temp_k = 1.0f / (0.001129148f + (0.000234125f * log_r) + (0.0000000876741f * log_r * log_r * log_r));
    return temp_k - 273.15f;
}

/* ------------------------------------------------------------------ */
/* PDP 协议状态机                                                      */
/* ------------------------------------------------------------------ */
#define MAX_LINE 4096
char line_buf[MAX_LINE];
int  line_len = 0;

/* 通过 Serial1 发送一行 JSON */
void pdp_send(const char *json) {
    Serial1.print(json);
    Serial1.print('\n');
    Serial1.flush();
}

/* 读取一行以换行符结尾的 JSON 到 line_buf。
   当完整行可用时返回 true。 */
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

/* 处理一个传入的 PDP 帧。
   握手完成并进入工作模式时返回 true。 */
bool operational = false;

void handle_frame(const char *json) {
    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        /* 忽略格式错误的帧 */
        return;
    }

    const char *type = doc["type"];
    if (!type) return;

    /* ---- 握手阶段 ---- */

    if (strcmp(type, "hello") == 0) {
        /* Brain 正在发起握手，验证协议版本 */
        const char *ver = doc["ver"];
        if (!ver || strcmp(ver, "1") != 0) {
            /* 不支持的协议版本 —— 不响应 */
            return;
        }
        /* 用我们的身份信息进行响应 */
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
        /* Brain 需要完整的 manifest */
        pdp_send(MANIFEST);
        return;
    }

    if (strcmp(type, "skill_req") == 0) {
        /* Brain 按索引请求技能文件 */
        int index = doc["index"] | -1;
        if (index == 0) {
            /* 构建技能帧：将技能内容作为 JSON 字符串值嵌入 */
            StaticJsonDocument<2048> skill_doc;
            skill_doc["type"] = "skill";
            skill_doc["name"] = "temp_sensor";
            skill_doc["content"] = SKILL_CONTENT;

            char skill_frame[2048];
            serializeJson(skill_doc, skill_frame, sizeof(skill_frame));
            pdp_send(skill_frame);
        }
        /* 忽略超出范围的索引 */
        return;
    }

    if (strcmp(type, "ready") == 0) {
        /* Brain 已完成所有技能的加载，确认并进入工作模式 */
        pdp_send("{\"type\":\"ready_ack\"}");
        operational = true;
        return;
    }

    /* ---- 工作阶段 ---- */

    if (strcmp(type, "tool_call") == 0 && operational) {
        const char *id   = doc["id"];
        const char *tool = doc["tool"];
        if (!id || !tool) return;

        StaticJsonDocument<512> result_doc;
        result_doc["type"] = "tool_result";
        result_doc["id"]   = id;  /* 回传调用 ID */

        if (strcmp(tool, "sensor_read") == 0) {
            float temp_c = read_temperature_celsius();

            char output[64];
            snprintf(output, sizeof(output), "Temperature: %.1f°C", temp_c);

            result_doc["ok"]     = true;
            result_doc["output"] = output;
        } else {
            /* 未知工具 —— 如果 manifest 正确，这种情况不应发生 */
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
    /* USB Serial 调试输出（可选） */
    Serial.begin(115200);
    Serial.println("MimiClaw PDP peripheral starting...");

    /* Serial1 用于 PDP 通信 */
    Serial1.begin(115200);

    /* DET 引脚：拉高以向 Brain 发送存在信号。
       将此 GPIO 通过 10k 电阻连接到 Brain 的 GPIO21，上拉至 3.3V。
       当此 MCU 上电并就绪时，DET 线保持高电平。 */
    /* 此处无需代码 —— 10k 上拉电阻被动完成此功能。 */

    Serial.println("Ready. Waiting for brain hello...");
}

void loop() {
    if (pdp_read_line()) {
        handle_frame(line_buf);
    }

    /* 在此添加您自己的后台工作（传感器轮询等）
       保持快速执行 —— loop() 中的阻塞不要超过几毫秒 */
}
```

### 测试示例代码

1. 将代码上传到您的 Arduino
2. 以 115200 波特率打开 Arduino 串口监视器（USB Serial）
3. 在 Brain Box 串口 CLI 中运行：`heap_info` 验证 Brain 正在运行
4. 将外设硬件连接到 6 针连接器
5. 观察 Brain 日志：您应该能看到握手序列，随后出现 `sensor_read` 工具

您可以在不使用 AI 的情况下，直接从 Brain CLI 测试工具：

```
mimi> tool_exec sensor_read '{}'
Temperature: 23.4°C
```

---

## 3. Manifest 编写指南

Manifest 是您的外设在握手阶段发送的完整能力声明。Brain Box 每次连接时读取一次；此处的准确性决定了 AI 能用您的硬件做什么。

### device 部分

```json
"device": {
    "name":         "my_device",
    "display_name": "My Device",
    "version":      "1.0.0",
    "description":  "One sentence about what this device does."
}
```

| 字段           | 规则                                                                                                                           |
|----------------|--------------------------------------------------------------------------------------------------------------------------------|
| `name`         | 仅使用 snake_case，只允许字母、数字和下划线，最多 63 个字符。用作 SPIFFS 路径的文件名前缀。若有多种外设类型，名称必须唯一。    |
| `display_name` | 可读名称，显示在 Brain 日志中，最多 127 个字符。                                                                               |
| `version`      | 语义化版本号：`MAJOR.MINOR.PATCH`。添加工具时递增 MINOR，破坏兼容性时递增 MAJOR。                                              |
| `description`  | 一句话，使用简明英语。帮助 AI 理解何时使用此外设。                                                                             |

### tools 数组

`tools` 中的每个条目定义一个可调用的操作：

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

#### 工具名称规则

- 仅使用 snake_case（小写字母、数字、下划线）
- 以您的设备名称为前缀：使用 `arm_move` 而非 `move`
- 最多 63 个字符
- 必须全局唯一 —— 不得使用 Brain 内置工具已占用的名称：`web_search`、`get_current_time`、`read_file`、`write_file`、`edit_file`、`list_dir`、`cron_add`、`cron_list`、`cron_remove`

#### 描述规则

描述是 Claude 决定何时调用您的工具的依据。请以清晰的祈使句形式编写，包含以下内容：

- 工具的功能（动词开头）
- 参数使用的单位（毫米、度、百分比等）
- 返回值的含义
- 任何重要约束条件

差：`"Moves arm."`
好：`"Move the arm end-effector to an absolute XYZ position in millimeters. Returns the actual travel time in seconds."`

#### 输入 Schema：支持的类型

Brain 将您的 `input_schema` 作为 JSON Schema 直接发送给 Claude。参数属性中支持的类型：

| 类型      | JSON Schema              | Arduino 类型  | 适用场景                         |
|-----------|--------------------------|---------------|----------------------------------|
| `string`  | `"type":"string"`        | `const char*` | 文本、枚举值、模式名称           |
| `number`  | `"type":"number"`        | `float`       | 小数：位置、温度                 |
| `integer` | `"type":"integer"`       | `int`         | 计数、索引、百分比               |
| `boolean` | `"type":"boolean"`       | `bool`        | 开/关标志                        |

为每个参数添加 `"description"` —— Claude 通过此字段了解应传入什么值。

为数值类型添加 `"minimum"` 和 `"maximum"` 提示，帮助 Claude 保持在合法范围内：

```json
"x": {
    "type": "number",
    "description": "X position in mm.",
    "minimum": -200,
    "maximum": 200
}
```

#### 必填与可选参数

在 `"required"` 中列出所有必填参数。不在 `required` 中的参数为可选 —— 您的固件应处理它们缺失的情况并应用默认值。

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

固件中的用法：`int force = doc["input"]["force"] | 80;`（缺失时默认为 80）。

### skills 数组

```json
"skills": ["robotic_arm", "gripper_safety"]
```

每个字符串必须与您在对应 `skill` 消息中返回的 `name` 字段完全匹配。Brain 按索引顺序（0、1、……）依次请求技能文件。

对于简单外设，一个技能文件即已足够。对于具有多个子系统的复杂外设（机械臂 + 夹持器 + 摄像头），请拆分为多个技能文件。

---

## 4. 技能文件编写指南

技能文件是 Markdown 文档，Brain 将其注入 Claude 的系统提示中。Claude 在每次对话开始时读取这些文件。优质的技能文档决定了 AI 能否自信地操作您的硬件，还是在操作中频频出错。

### 格式

纯 Markdown。Brain 将整个文件作为文本块注入系统提示中，无需特殊语法。

技能文件保持在 4,000 个字符以内。过长的技能文件会消耗上下文窗口并降低 AI 的响应速度。

### 应包含的内容

**1. 设备描述（1-2 句）**

描述设备是什么、能做什么，以及用户何时可能需要使用它。

**2. 工具使用示例**

针对每个工具：给出一个自然语言示例和对应的工具调用，让 Claude 学会从意图到动作的映射。

**3. 组合模式**

如果工具设计为按顺序使用（归位 → 移动 → 抓取），请明确记录该模式。Claude 会按此复现。

**4. 安全注意事项**

AI 必须遵守的任何约束：最大行程限制、必要的操作顺序、危险操作。Claude 会认真对待安全注意事项。

**5. 单位约定**

明确声明单位。"毫米"与"厘米"的歧义会导致错误的调用。

### 示例：机械臂技能文件

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

## 5. 工具实现指南

### 解析 tool_call

每个 `tool_call` 帧具有以下结构：

```json
{"type":"tool_call","id":"a1b2c3d4","tool":"arm_move","input":{"x":100,"y":50,"z":200}}
```

您的固件必须读取以下三个字段：

| 字段   | 读取方式（ArduinoJson）              | 说明                           |
|--------|--------------------------------------|--------------------------------|
| `id`   | `const char *id = doc["id"];`       | 在 tool_result 中原样回传      |
| `tool` | `const char *tool = doc["tool"];`   | 分派到对应处理函数             |
| `input`| `JsonObject input = doc["input"];`  | 从此处提取参数                 |

### 读取参数

```cpp
JsonObject input = doc["input"];

/* 必填参数 —— 无需默认值 */
float x = input["x"];
float y = input["y"];
float z = input["z"];

/* 带默认值的可选参数 */
int speed = input["speed"] | 50;    /* 默认值：50 */

/* 布尔参数 */
bool enabled = input["enabled"] | true;

/* 字符串参数 */
const char *mode = input["mode"] | "normal";
```

在将数值输入传递给硬件之前，始终进行范围钳位：

```cpp
z = constrain(z, 0.0f, 300.0f);  /* 强制执行 Z 限制 */
speed = constrain(speed, 1, 100);
```

### 构造 tool_result

**成功时：**

```cpp
StaticJsonDocument<512> result;
result["type"]   = "tool_result";
result["id"]     = id;          /* 回传调用 ID */
result["ok"]     = true;
result["output"] = "Moved to (100, 50, 200) in 1.2s";

char frame[512];
serializeJson(result, frame, sizeof(frame));
pdp_send(frame);
```

**失败时：**

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

### `output` 字符串

`output` 字符串作为工具结果直接返回给 Claude。请将其写成 AI 可以融入回复的可读句子：

好：`"Moved to (100, 50, 200) in 1.2 seconds. Gripper is open."`
差：`"OK"` —— 过于简短，Claude 无法形成有效回复
差：`{"x":100,"y":50}` —— 原始 JSON 会让 Claude 困惑，它期望的是自然语言

### 关于调用 ID 的生成

您无需生成调用 ID —— Brain 会生成它。您只需将 `tool_call` 中收到的 `id` 值原样回传。这让 Brain 能将响应与请求对应起来。

```cpp
const char *id = doc["id"];
/* ... 执行操作 ... */
result["id"] = id;  /* 原样回传，不要修改 */
```

### 错误处理

在以下任何情况下，返回 `ok=false` 的 `tool_result`：

- 参数超出有效范围
- 硬件故障（电机堵转、传感器失效）
- 操作被安全约束阻止
- 未知工具名称

不要让任何 `tool_call` 无响应。如果您的 MCU 进入未知状态，发送失败结果并尝试恢复：

```cpp
/* 紧急情况：执行过程中硬件报错 */
result["ok"]    = false;
result["error"] = "Motor stall detected on joint 3. Attempting home.";
pdp_send(frame);
arm_home();  /* 尝试恢复 */
```

### 超时注意事项

Brain 等待 `MIMI_PERIPH_TOOL_TIMEOUT_MS` = 10 秒来接收 `tool_result`。如果您的操作耗时超过 10 秒：

**方案 A**：将其拆分为更小的操作。例如，与其让 `arm_move_slow` 耗时 15 秒，不如让 AI 两次调用 `arm_move` 到中间位置。

**方案 B**：立即返回部分结果，然后在后续轮次中通过 AI 调用 `write_file` 工具更新 SPIFFS。（AI 可以在下一轮使用 `sensor_read` 工具检查状态。）

**方案 C**：在 `mimi_config.h` 中增大 `MIMI_PERIPH_TOOL_TIMEOUT_MS` 并重新编译 Brain 固件。

---

## 6. 进阶：多工具协同

### 顺序工具调用

Brain 通过 UART 一次发送一个工具调用。Claude 每次代理迭代可能调用多个工具，但 Brain 会将它们序列化处理 —— 等收到 `tool_result` 后才发送下一个 `tool_call`。

这意味着您的 MCU 最多同时处理一个工具调用，无需加锁。

Brain 发出的典型多工具调用序列：

```
brain → {"type":"tool_call","id":"aa01","tool":"arm_home",...}
brain ← {"type":"tool_result","id":"aa01","ok":true,"output":"Homed."}
brain → {"type":"tool_call","id":"bb02","tool":"arm_move","input":{"x":50,"y":0,"z":100}}
brain ← {"type":"tool_result","id":"bb02","ok":true,"output":"Moved in 0.8s"}
brain → {"type":"tool_call","id":"cc03","tool":"arm_grip","input":{"width":0}}
brain ← {"type":"tool_result","id":"cc03","ok":true,"output":"Gripper closed."}
```

### 有状态操作

当您的固件在工具调用之间维护状态（例如跟踪夹持器是开还是关）时，请在 `output` 字符串中体现该状态，以便 Claude 进行推理：

```cpp
static bool gripper_open = true;

/* 在 arm_grip 处理函数中： */
gripper_open = (width > 10);
snprintf(output, sizeof(output),
    "Gripper %s. Width: %d.", gripper_open ? "open" : "closed", width);
```

这让 Claude 能给出准确的状态报告："夹持器当前处于关闭状态并正在夹持方块。"

### 表达错误恢复状态

如果工具失败并需要操作员介入，请在错误字符串中包含恢复说明：

```cpp
result["error"] = "Servo overload on joint 2. Power cycle the arm, "
                  "then call arm_home() to resume.";
```

Claude 会将此内容原样转达给用户，使用户能够采取纠正措施。

### 通过 PDP 进行固件更新

如果您的外设需要固件更新，可以实现一个 `ota_start` 工具，接收 URL，下载固件并刷写。AI 可以根据用户请求触发此操作：

```
User: "Update the arm firmware to version 2.0"
Claude: (调用 ota_start 并传入 URL)
Arm: 下载并刷写固件。返回："OTA complete. Rebooting."
```

重启后，机械臂重新连接，Brain 执行全新握手。

---

## 7. 使用 Python 测试

使用以下 Python 脚本通过 USB 转串口适配器模拟外设，以便在真实固件就绪之前测试 Brain 侧的 PDP 实现。

### 依赖

```bash
pip install pyserial
```

### 模拟器脚本

```python
#!/usr/bin/env python3
"""
MimiClaw PDP 外设模拟器

通过串口模拟温度传感器外设。
使用 USB 转串口适配器连接到 Brain Box UART1 引脚：
  - 适配器 TX -> Brain GPIO18 (RX)
  - 适配器 RX -> Brain GPIO17 (TX)
  - 适配器 GND -> Brain GND

用法：
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
    """模拟带有轻微漂移的温度读数。"""
    return base_temp + random.uniform(-0.5, 0.5)


def send_frame(ser, obj):
    """将 obj 序列化为 JSON 并以换行符结尾的帧形式写出。"""
    line = json.dumps(obj) + '\n'
    ser.write(line.encode('utf-8'))
    ser.flush()
    print(f"  SENT: {line.strip()}")


def handle_frame(ser, line):
    """处理一个收到的 PDP 帧。"""
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
            # 读取可用字节
            data = ser.read(256).decode('utf-8', errors='replace')
            if data:
                buffer += data
                # 处理完整的行
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    handle_frame(ser, line)

            # 检查报警阈值（后台模拟）
            if alarm_threshold is not None:
                temp = read_temperature()
                if temp > alarm_threshold:
                    print(f"  [alarm] Temperature {temp:.1f}°C exceeds threshold {alarm_threshold:.1f}°C")

            time.sleep(0.01)


if __name__ == "__main__":
    main()
```

### 运行模拟器

```bash
# 将 USB 转串口适配器连接到 Brain UART1 引脚
# Brain TX (GPIO17) -> 适配器 RX
# Brain RX (GPIO18) -> 适配器 TX
# Brain GND         -> 适配器 GND

# 运行模拟器
python3 pdp_simulator.py /dev/cu.usbserial-0001

# 注意：您还需要模拟 DET 引脚拉高。
# 将适配器 3.3V 引脚（或台式电源）的 3.3V 电压
# 通过 10k 电阻连接到 Brain GPIO21，以发出连接信号。
```

### 预期输出

当 Brain 检测到 DET 引脚并建立连接时：

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

在 Brain 侧，您可以通过发送 Telegram 消息"当前温度是多少？"来验证。Claude 会调用 `sensor_read` 并以模拟读数进行响应。

---

## 8. 支持的 MCU 平台

### ESP32（推荐用于复杂外设）

适合需要 WiFi、BLE、两个以上 UART、I2S、摄像头或大量计算的外设的最佳选择。

```cpp
// ESP32 Arduino —— Serial1 默认在 GPIO16/17
// 如需重新分配到安全引脚：
Serial1.begin(115200, SERIAL_8N1, 26, 27);  // RX=26, TX=27
```

优势：
- 4 个硬件 UART
- 240 MHz 双核处理器
- 520 KB SRAM + 可选 PSRAM
- 完整的 ArduinoJson 支持
- OTA 固件更新
- ESP-IDF 内置原生 cJSON 库（若使用 ESP-IDF 而非 Arduino）

注意事项：
- 新设计建议使用 ESP32-S3 或 ESP32-C3（货源更稳定）
- 若使用 ESP-IDF 而非 Arduino，请用 cJSON（IDF 内置）替换 ArduinoJson

### Arduino Uno / Nano（仅适用于简单传感器）

仅适合工具数量为 1-2 个、无复杂计算的简单外设。

```cpp
// Uno 只有一个硬件 UART（被 USB 占用）。
// 在引脚 10/11 上使用 SoftwareSerial 进行 PDP 通信：
#include <SoftwareSerial.h>
SoftwareSerial pdp_serial(10, 11);  // RX=10, TX=11
pdp_serial.begin(115200);

// 警告：在 8 MHz 板上，SoftwareSerial 在 115200 波特率下不可靠。
// 改用 9600 波特率，并在 mimi_config.h 中更新 MIMI_PERIPH_UART_BAUD。
```

局限性：
- 2 KB SRAM —— `MAX_LINE = 4096` 的缓冲区放不下。将其缩小到 512 字节并保持 manifest 精简。
- SoftwareSerial 在 115200 波特率下不可靠；请使用 9600 波特率并重新编译 Brain。
- `StaticJsonDocument<512>` 的 ArduinoJson 可行。
- 最多支持 1-2 个具有小型输入 Schema 的简单工具。

对于超出简单传感器范围的用途，请使用 Arduino Mega 或改用 ESP32。

### Arduino Mega

PDP 外设中最佳的传统 Arduino 选择：

```cpp
// Mega 的 Serial1 在引脚 18/19，Serial2 在 16/17，Serial3 在 14/15
// 使用 Serial1（硬件 UART，支持完整的 115200）：
Serial1.begin(115200);
```

相比 Uno 的优势：
- 硬件 Serial1/2/3（稳定的 115200）
- 8 KB SRAM —— 足以容纳 `MAX_LINE = 4096`
- ArduinoJson `StaticJsonDocument<4096>` 可放入 SRAM
- 54 个数字 I/O 引脚，适合复杂的执行器阵列

### STM32（STM32F1/F4/G0）

实时控制应用的高性能选择：

```cpp
// STM32 Arduino (STM32duino) —— USART1 在 PA9/PA10
HardwareSerial pdp_serial(PA10, PA9);  // RX, TX
pdp_serial.begin(115200);
```

使用 STM32 HAL（裸机）：

```c
/* USART2 示例 (PA2=TX, PA3=RX) */
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

若不使用 ArduinoJson，可使用 **jsmn** 进行 JSON 解析（单头文件，MIT 许可证）：

```c
#include "jsmn.h"
jsmn_parser parser;
jsmntok_t tokens[64];
jsmn_init(&parser);
int count = jsmn_parse(&parser, line_buf, strlen(line_buf), tokens, 64);
/* 遍历 token 以提取 "type"、"id"、"tool"、"input" 字段 */
```

优势：
- 72-480 MHz Cortex-M —— 足够快以实现实时电机控制
- 128 KB - 2 MB Flash，20-512 KB SRAM
- 多个硬件 UART、DMA、硬件定时器
- 适用于伺服控制器、步进电机驱动器、CNC 应用

### RP2040（Raspberry Pi Pico）

成本、能力和易用性的优秀平衡：

```cpp
// Arduino-Pico：Serial1 默认在 GPIO0(TX)/GPIO1(RX)
// 为 PDP 重新分配：
Serial1.setTX(4);  // GPIO4 = TX 连接到 Brain GPIO18
Serial1.setRX(5);  // GPIO5 = RX 连接自 Brain GPIO17
Serial1.begin(115200);
```

使用 Pico SDK（裸机）：

```c
#include "pico/stdlib.h"
#include "hardware/uart.h"

uart_init(uart1, 115200);
gpio_set_function(4, GPIO_FUNC_UART);  /* TX */
gpio_set_function(5, GPIO_FUNC_UART);  /* RX */
uart_set_hw_flow(uart1, false, false);
uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
```

优势：
- 133 MHz 双核 ARM Cortex-M0+
- 264 KB SRAM —— 远超 4 KB 行缓冲区的需求
- PIO 状态机，用于精确 PWM/计时
- 极低成本（约 1 美元）
- 支持 MicroPython，便于快速原型开发

MicroPython PDP 实现：

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
            # ... 处理 msg ...
```

### 平台对比汇总

| 平台             | UART     | SRAM     | JSON 库       | 推荐用途                                     |
|------------------|----------|----------|---------------|----------------------------------------------|
| ESP32/S3         | 硬件     | 520 KB+  | ArduinoJson 6 | 复杂外设、WiFi/BLE、摄像头                   |
| RP2040 (Pico)    | 硬件     | 264 KB   | ArduinoJson 6 | 通用外设、电机控制                           |
| STM32F4          | 硬件     | 192 KB+  | jsmn          | 实时控制、伺服/步进电机系统                  |
| Arduino Mega     | 硬件     | 8 KB     | ArduinoJson 6 | 多 I/O、传统 Arduino 生态                    |
| STM32G0          | 硬件     | 36 KB    | jsmn          | 低成本实时控制                               |
| Arduino Uno/Nano | 软件     | 2 KB     | ArduinoJson 6 | 仅适用于简单传感器（受限）                   |
