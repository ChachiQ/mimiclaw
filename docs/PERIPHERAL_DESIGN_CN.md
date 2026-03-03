# MimiClaw 外设设计文档

> MimiClaw 模块化主控脑 / 外设系统技术设计文档。
> 目标硬件：ESP32-S3 主控脑盒 + 外部外设 MCU。

---

## 目录

1. [系统概览](#1-系统概览)
2. [物理连接规格](#2-物理连接规格)
3. [软件模块架构](#3-软件模块架构)
4. [PDP v1 协议规范](#4-pdp-v1-协议规范)
5. [清单格式规范](#5-清单格式规范)
6. [数据流架构](#6-数据流架构)
7. [SPIFFS 存储路径](#7-spiffs-存储路径)
8. [设计原则](#8-设计原则)
9. [mimi_config.h 常量参考](#9-mimi_configh-常量参考)

---

## 1. 系统概览

MimiClaw 采用三层架构，将 AI 智能核心与可选硬件扩展模块解耦分离。

### 层级模型

```
第三层：外设设备（热插拔，可选）
  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────────┐
  │  机械臂          │  │  传感器阵列      │  │  显示模块        │
  │  (STM32/ESP32)  │  │  (Arduino Mega) │  │  (RP2040)        │
  └────────┬────────┘  └────────┬────────┘  └────────┬─────────┘
           │                   │                     │
           └───────────────────┴─────────────────────┘
                        6 针磁吸连接器（PDP 协议）
                                   │
第二层：语音 I/O（可选硬件，编译期启用）
  ┌────────────────────────────────┴─────────────────────────────┐
  │  INMP441 MEMS 麦克风（I2S）         MAX98357A 扬声器（I2S）   │
  │  GPIO 4/5/6                         GPIO 15/16/7             │
  └────────────────────────────────┬─────────────────────────────┘
                                   │
第一层：核心主控脑（始终运行，始终自主）
  ┌────────────────────────────────┴─────────────────────────────┐
  │                     ESP32-S3 主控脑盒                        │
  │                                                              │
  │  核心 0（I/O）                   核心 1（AI 智能体）          │
  │  ┌──────────────┐               ┌────────────────────────┐  │
  │  │ Telegram 轮询│               │     Agent Loop         │  │
  │  │ WS 服务器    │──inbound_q───▶│  ReAct + LLM Proxy     │  │
  │  │ 串口 CLI     │               │  Tool Registry (9+N)   │  │
  │  │ 外设管理器   │◀──outbound_q──│  Context Builder       │  │
  │  └──────────────┘               └────────────────────────┘  │
  │                                                              │
  │  SPIFFS（12 MB）                                             │
  │  config/ memory/ sessions/ skills/ peripheral/ cron.json    │
  └──────────────────────────────────────────────────────────────┘
```

### 主控脑盒：始终独立运行

第一层完全自包含。无论是否连接任何外设或语音硬件，主控脑盒都会运行 Telegram 轮询、WebSocket 服务器以及完整的 ReAct 智能体循环。外设连接用于扩展能力，而非启用能力。

### 外设所提供的扩展

当外设连接时：

- 外设通过 PDP 握手宣告自身身份
- 其工具（如 `arm_move`、`arm_grip`）被动态注册到工具注册表中
- 其技能文件被写入 SPIFFS 并包含在系统提示词中
- 智能体可以立即使用新工具，无需重启

当外设断开时：

- 其工具从工具注册表中注销
- 其技能文件从活跃技能集中移除
- 主控脑盒继续正常运行，使用剩余的工具

---

## 2. 物理连接规格

### 6 针磁吸连接器引脚定义

```
引脚  信号   方向               GPIO     说明
─────────────────────────────────────────────────────────────────
 1   VCC     主控脑 → 外设      —        3.3 V 电源（最大 300 mA）
 2   GND     —                  —        公共地
 3   TX      主控脑 → 外设      GPIO 17  UART1 发送（主控脑发送命令）
 4   RX      外设 → 主控脑      GPIO 18  UART1 接收（主控脑接收结果）
 5   DET     外设 → 主控脑      GPIO 21  检测引脚：悬空/低电平 = 断开，
                                         外设拉高 = 已连接
 6   RST     主控脑 → 外设      —        可选开漏复位线
                                         （主控脑拉低以复位外设）
```

推荐使用磁吸连接器以实现安全热插拔（如 POGO 针或磁吸 USB-C 转接板）。引脚顺序将电源引脚（1、2）置于连接器的物理两端，信号引脚居中，以降低插拔时发生电源短路的风险。

### UART 参数

| 参数              | 值              |
|-------------------|-----------------|
| 端口              | UART1           |
| 波特率            | 115,200         |
| 数据位            | 8               |
| 奇偶校验          | 无              |
| 停止位            | 1               |
| 硬件流控          | 无（不使用 RTS/CTS） |
| 逻辑电平          | 3.3 V           |
| 最大帧大小        | 4,096 字节      |
| 帧分隔符          | `\n` (0x0A)     |

UART0（GPIO 43/44）是串口 CLI 使用的 USB-JTAG 控制台，请勿将其用于外设通信。

### DET 引脚行为

```
主控脑 GPIO 21 配置如下：
  - 启用内部下拉电阻
  - 配置上升沿（连接）和下降沿（断开）GPIO 中断

外设侧：
  - 上电时通过 10 kΩ 电阻将 DET 引脚拉高至 3.3 V
  - 在连接/断开时产生清晰的信号跳变
```

### 电缆长度

在 3.3 V 逻辑电平、115,200 波特率条件下，推荐最大电缆长度为 30 cm。若需更长距离，请降低波特率或添加 RS-485 电平转换器。

---

## 3. 软件模块架构

### 新增模块

#### `main/peripheral/peripheral_uart.h/c`

UART1 驱动。将 GPIO 17/18 初始化为 UART1 TX/RX，配置 115200-8N1，并提供面向行的读写函数。

```c
esp_err_t peripheral_uart_init(void);
esp_err_t peripheral_uart_write_line(const char *json_line);
esp_err_t peripheral_uart_read_line(char *buf, size_t buf_size, uint32_t timeout_ms);
void      peripheral_uart_flush(void);
```

内部 4 KB 环形缓冲区（`MIMI_PERIPH_UART_BUF_SIZE`），通过 `uart_driver_install` 实现事件驱动接收。帧以 `\n` 作为分隔符。`peripheral_uart_read_line` 阻塞直到收到换行符或超时。

#### `main/peripheral/peripheral_detector.h/c`

基于 GPIO 中断的热插拔检测，监测 GPIO 21。对 DET 信号进行 50 ms 去抖，以忽略磁吸接触时的瞬态抖动。

```c
esp_err_t peripheral_detector_init(void);
bool      peripheral_detector_is_connected(void);

/* 连接/断开事件的回调类型 */
typedef void (*peripheral_event_cb_t)(bool connected);
void peripheral_detector_set_callback(peripheral_event_cb_t cb);
```

中断在两个边沿均触发。单次 FreeRTOS 定时器处理去抖：50 ms 后的最终稳定状态上报给已注册的回调函数。

#### `main/peripheral/peripheral_protocol.h/c`

PDP v1 协议状态机。实现完整的握手序列：hello → ack → 清单交换 → 技能传输 → ready 确认。

```c
typedef struct {
    char  name[64];
    char  display_name[128];
    char  version[16];
    int   tool_count;
    int   skill_count;
    char  manifest_json[4096];   /* 完整清单，从 PSRAM 分配 */
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

所有 JSON 帧使用 `cJSON` 构建，并通过 `peripheral_uart_write_line` 发送。接收到的帧使用 `cJSON_Parse` 解析。

#### `main/peripheral/peripheral_manager.h/c`

外设生命周期管理器。作为 FreeRTOS 任务运行于核心 0。监听来自 `peripheral_detector` 的连接/断开事件，驱动 PDP 握手，将技能文件写入 SPIFFS，在工具注册表中注册/注销工具，并触发工具 JSON 重建。

```c
esp_err_t peripheral_manager_init(void);
esp_err_t peripheral_manager_start(void);
bool      peripheral_manager_is_ready(void);
const char *peripheral_manager_get_name(void);

/* 由 tool_peripheral.c 调用，发送 UART RPC 调用 */
esp_err_t peripheral_manager_tool_call(const char *tool_name,
                                        const char *input_json,
                                        char *output, size_t output_size);
```

管理器任务运行一个状态机：

```
IDLE ──连接──▶ HANDSHAKING ──成功──▶ READY ──断开──▶ IDLE
                    │                           │
                    └──超时/错误──▶ ERROR ───────┘
                            （记录警告并返回 IDLE）
```

初始化失败（UART 错误、握手超时）记录为警告并返回 IDLE，不会中止 `app_main`。

#### `main/tools/tool_peripheral.h/c`

动态工具 RPC 存根。对于外设清单中声明的每个工具，`peripheral_manager` 创建一个 `mimi_tool_t`，其 `execute` 函数指针调用 `peripheral_manager_tool_call`。这将调用通过 UART 路由，并阻塞直到收到结果或 `MIMI_PERIPH_TOOL_TIMEOUT_MS`（10 秒）超时。

```c
/* 由 peripheral_manager 调用，为一个清单工具创建存根 */
mimi_tool_t *tool_peripheral_create_stub(const char *tool_name,
                                          const char *description,
                                          const char *input_schema_json);
void tool_peripheral_free_stub(mimi_tool_t *stub);
```

存根的执行函数：

```c
static esp_err_t peripheral_tool_execute(const char *input_json,
                                          char *output, size_t output_size)
{
    return peripheral_manager_tool_call(tool_name, input_json,
                                        output, output_size);
}
```

#### `main/voice/voice_input.h/c`

通过 I2S 采集麦克风音频（INMP441，I2S_NUM_0，GPIO 4/5/6）。录制 16 kHz 16 位单声道 PCM。实现语音活动检测（VAD）：静音持续 `MIMI_VOICE_VAD_SILENCE_MS`（1500 ms）后停止录制，或在 `MIMI_VOICE_MAX_REC_MS`（10 秒）时强制停止。通过 HTTPS 将音频发送至 STT 服务商（DashScope paraformer 或 OpenAI Whisper）并返回转录文本。

```c
esp_err_t voice_input_init(void);
esp_err_t voice_input_start(void);   /* 启动 FreeRTOS 任务 */
esp_err_t voice_input_record_and_transcribe(char *text_out, size_t text_size);
```

录音缓冲区（`MIMI_VOICE_REC_BUF_SIZE` = 160 KB）从 PSRAM 分配。

#### `main/voice/voice_output.h/c`

通过 STT 服务商（DashScope CosyVoice 或 OpenAI TTS）进行文字转语音合成，然后通过 I2S 播放 MP3/PCM 音频（MAX98357A，I2S_NUM_1，GPIO 15/16/7）。使用 minimp3 单头文件库进行 MP3 解码。

```c
esp_err_t voice_output_init(void);
esp_err_t voice_output_start(void);  /* 启动 FreeRTOS 任务 */
esp_err_t voice_output_speak(const char *text);
```

#### `main/voice/voice_channel.h/c`

语音通道协调器。监听按键（GPIO 0）的按下事件，触发 `voice_input_record_and_transcribe`，将转录结果作为 `mimi_msg_t` 推入入站队列，`channel` 字段设为 `MIMI_CHAN_VOICE`。出站分发任务将 `"voice"` 消息路由到 `voice_output_speak`。

```c
esp_err_t voice_channel_init(void);
esp_err_t voice_channel_start(void);
```

### 修改的模块

#### `main/tools/tool_registry.c`

向 `mimi_tool_t` 添加了 `is_dynamic` 字段（已在 `tool_registry.h` 中声明）。新增了 `tool_registry_register_dynamic`、`tool_registry_unregister_peripheral_tools` 和 `tool_registry_rebuild_json` API，供 `peripheral_manager` 在运行时无需重启地添加/移除工具。

变更摘要：静态工具表新增运行时可变区段。每次动态注册/注销事件后重建工具 JSON 字符串。

#### `main/mimi.c`

在现有初始化序列之后添加了外设和语音子系统的初始化调用。两者均以非致命方式处理失败：

```c
/* 外设子系统（非致命） */
if (peripheral_manager_init() == ESP_OK) {
    peripheral_manager_start();
}

/* 语音子系统（非致命，仅在 CONFIG_MIMI_VOICE_ENABLE=y 时生效） */
#if CONFIG_MIMI_VOICE_ENABLE
if (voice_channel_init() == ESP_OK) {
    voice_channel_start();
}
#endif
```

在 `outbound_dispatch_task` 中新增了 `"voice"` 通道路由：

```c
} else if (strcmp(msg.channel, MIMI_CHAN_VOICE) == 0) {
    voice_output_speak(msg.content);
}
```

#### `main/agent/context_builder.c`

技能摘要已包含 `/spiffs/skills/` 下的所有文件。外设技能（在连接时写入为 `/spiffs/skills/peripheral_<name>.md`）会在下次上下文构建时自动包含，无需修改代码。

#### `main/CMakeLists.txt`

需添加到 `SRCS` 的新源文件：

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

需添加到 `REQUIRES` 的新 IDF 组件：

```cmake
driver   # GPIO 中断、UART 驱动、I2S 驱动
```

---

## 4. PDP v1 协议规范

PDP（外设设备协议）是基于 UART1 的换行符分隔 JSON 协议。

### 帧格式

```
<JSON 对象>\n
```

- 每条消息为一个以单个 `\n`（0x0A）结尾的 JSON 对象
- 最大帧大小：4,096 字节（双方均强制执行）
- 字符编码：UTF-8
- 无二进制分帧、无长度前缀、无 CRC（UART 奇偶校验提供基本错误检测）
- 双方均丢弃超过 4,096 字节的帧并记录警告

### 消息类型

#### `hello` — 主控脑发起握手

在 DET 信号变高且去抖完成后，由主控脑立即发送。

```json
{"type":"hello","ver":"1"}
```

| 字段  | 类型   | 说明                                 |
|-------|--------|--------------------------------------|
| type  | string | 固定为 `"hello"`                     |
| ver   | string | 协议版本。主控脑支持 `"1"`。         |

#### `ack` — 外设确认 hello

由外设响应 `hello` 时发送。宣告身份和工具数量。

```json
{"type":"ack","name":"robotic_arm","display_name":"6-DOF Robotic Arm","ver":"1","tools":3,"skills":1}
```

| 字段         | 类型   | 说明                                              |
|--------------|--------|---------------------------------------------------|
| type         | string | 固定为 `"ack"`                                    |
| name         | string | 机器名称（snake_case，用于文件路径）              |
| display_name | string | 人类可读名称（显示在日志中）                      |
| ver          | string | 外设固件版本（语义化版本号）                      |
| tools        | int    | 清单中的工具数量                                  |
| skills       | int    | 可用技能文件数量                                  |

#### `manifest_req` — 主控脑请求完整清单

```json
{"type":"manifest_req"}
```

#### `manifest` — 外设发送完整清单

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

清单必须在单个 UART 帧内传输（4,096 字节）。对于工具数量较多的外设，请保持描述简洁。

#### `skill_req` — 主控脑按索引请求技能文件

```json
{"type":"skill_req","index":0}
```

| 字段  | 类型 | 说明                                           |
|-------|------|------------------------------------------------|
| index | int  | 清单 `skills` 数组中的从零开始的索引           |

#### `skill` — 外设发送技能文件内容

```json
{"type":"skill","name":"robotic_arm","content":"# Robotic Arm\n\nThis peripheral provides a 6-DOF robotic arm.\n\n## Usage\n- Use `arm_move` to position..."}
```

| 字段    | 类型   | 说明                                                    |
|---------|--------|---------------------------------------------------------|
| name    | string | 技能名称（与清单 `skills` 数组中的条目匹配）            |
| content | string | 技能文件的完整 Markdown 内容（换行符表示为 `\n`）       |

主控脑将内容写入 `/spiffs/skills/peripheral_<name>.md`。

#### `ready` — 主控脑发出握手完成信号

在所有技能文件接收并写入 SPIFFS 后发送。

```json
{"type":"ready"}
```

#### `ready_ack` — 外设确认 ready

```json
{"type":"ready_ack"}
```

收到 `ready_ack` 后，双方切换至运行模式。主控脑此后可发送 `tool_call` 帧。

#### `tool_call` — 主控脑请求执行工具

```json
{"type":"tool_call","id":"abc123","tool":"arm_move","input":{"x":100,"y":50,"z":200}}
```

| 字段  | 类型   | 说明                                                         |
|-------|--------|--------------------------------------------------------------|
| id    | string | 主控脑生成的唯一调用 ID（8 位十六进制字符串）                |
| tool  | string | 工具名称（必须与清单中声明的工具匹配）                       |
| input | object | 工具输入参数，符合该工具的 `input_schema`                    |

主控脑将 `id` 生成为随机 8 字符十六进制字符串。同一时刻只有一个 `tool_call` 处于挂起状态——主控脑等待 `tool_result` 后再发送下一个调用。

#### `tool_result` — 外设返回执行结果（成功）

```json
{"type":"tool_result","id":"abc123","ok":true,"output":"Moved to (100, 50, 200) in 1.2s"}
```

#### `tool_result` — 外设返回执行结果（失败）

```json
{"type":"tool_result","id":"abc123","ok":false,"error":"Joint angle out of range: z=200 exceeds max 180mm"}
```

| 字段   | 类型   | 说明                                               |
|--------|--------|----------------------------------------------------|
| id     | string | 对应 `tool_call` 中 `id` 的回显                    |
| ok     | bool   | 成功为 `true`，失败为 `false`                      |
| output | string | 人类可读的结果（`ok=true` 时存在）                 |
| error  | string | 错误描述（`ok=false` 时存在）                      |

### 错误处理

| 情况                                    | 主控脑行为                                                 |
|-----------------------------------------|------------------------------------------------------------|
| 发送 `hello` 后 5 秒内无 `ack`          | 记录警告，返回 IDLE（外设不支持该协议）                   |
| 发送 `manifest_req` 后 5 秒内无 `manifest` | 记录警告，返回 IDLE                                    |
| 发送 `skill_req` 后 5 秒内无 `skill`    | 记录警告，返回 IDLE                                        |
| 10 秒内无 `tool_result`                 | 向智能体返回错误字符串：`"Peripheral timeout"`            |
| `tool_result` 中 `ok=false`             | 将 `error` 字符串作为工具结果内容返回给智能体             |
| 帧超过 4,096 字节                       | 记录警告，丢弃帧，继续运行                                 |
| 无效 JSON 帧                            | 记录警告，丢弃帧，继续运行                                 |
| 工具调用期间 DET 变低                   | 取消挂起调用，注销工具，返回 IDLE                          |

### 握手时序图

```
Brain (ESP32-S3)                       Peripheral (MCU)
       │                                      │
       │  [DET 引脚变高]                      │
       │  [50ms 去抖完成]                     │
       │                                      │
       │──── {"type":"hello","ver":"1"} ─────▶│
       │                                      │  [验证协议版本]
       │◀─── {"type":"ack","name":"robotic_arm","tools":3,"skills":1} ──│
       │                                      │
       │──── {"type":"manifest_req"} ────────▶│
       │                                      │
       │◀─── {"type":"manifest",...} ─────────│
       │                                      │
       │  [解析清单，创建工具存根]             │
       │                                      │
       │  [遍历技能索引 0..N-1]               │
       │──── {"type":"skill_req","index":0} ─▶│
       │◀─── {"type":"skill","name":"robotic_arm","content":"..."} ─────│
       │  [写入 /spiffs/skills/peripheral_robotic_arm.md]               │
       │                                      │
       │──── {"type":"ready"} ───────────────▶│
       │◀─── {"type":"ready_ack"} ────────────│
       │                                      │
       │  [在 tool_registry 中注册 N 个工具]  │
       │  [重建工具 JSON]                     │
       │  [peripheral_manager 状态 = READY]   │
       │                                      │
       │  ... 运行模式：工具调用 ...          │
       │                                      │
       │──── {"type":"tool_call","id":"a1b2","tool":"arm_move",         │
       │      "input":{"x":100,"y":50,"z":200}} ────────────────────────▶│
       │                                      │  [执行 arm_move()]
       │◀─── {"type":"tool_result","id":"a1b2","ok":true,              │
       │      "output":"Moved to (100,50,200) in 1.2s"} ───────────────│
       │                                      │
       │  [DET 引脚变低]                      │
       │  [50ms 去抖完成]                     │
       │  [注销外设工具]                      │
       │  [从 SPIFFS 删除技能文件]            │
       │  [peripheral_manager 状态 = IDLE]    │
```

---

## 5. 清单格式规范

清单是外设能力的完整声明。在握手期间作为 `manifest` 帧的载荷发送。

### JSON Schema

```json
{
  "type": "manifest",
  "device": {
    "name":         "<string: snake_case 标识符>",
    "display_name": "<string: 人类可读名称>",
    "version":      "<string: 语义化版本号，如 1.0.0>",
    "description":  "<string: 单行描述>"
  },
  "tools": [
    {
      "name":        "<string: snake_case 工具名称>",
      "description": "<string: 工具功能描述，供 LLM 理解>",
      "input_schema": {
        "type": "object",
        "properties": {
          "<param_name>": {
            "type":        "<string|number|integer|boolean>",
            "description": "<string: 参数描述>"
          }
        },
        "required": ["<param_name>", ...]
      }
    }
  ],
  "skills": ["<skill_name>", ...]
}
```

### 字段说明

| 字段                              | 是否必填 | 说明                                                              |
|-----------------------------------|----------|-------------------------------------------------------------------|
| `device.name`                     | 是       | 机器标识符。用于文件路径。snake_case。最多 63 个字符。            |
| `device.display_name`             | 是       | 人类可读名称，显示在日志中。                                      |
| `device.version`                  | 是       | 语义化版本号格式的固件版本（`MAJOR.MINOR.PATCH`）。               |
| `device.description`              | 是       | 外设功能的单行描述。                                              |
| `tools`                           | 是       | 工具定义数组。可为空（`[]`）。                                    |
| `tools[].name`                    | 是       | 发送给 LLM 的工具名称。snake_case。建议以设备名称为前缀。         |
| `tools[].description`             | 是       | 工具的自然语言描述，供 LLM 理解。                                 |
| `tools[].input_schema`            | 是       | 描述输入参数的 JSON Schema 对象。                                 |
| `tools[].input_schema.type`       | 是       | 固定为 `"object"`。                                               |
| `tools[].input_schema.properties` | 是       | 将参数名称映射到类型描述符的对象。                                |
| `tools[].input_schema.required`   | 是       | 必填参数名称的数组（可为空 `[]`）。                               |
| `skills`                          | 是       | 技能名称数组（每个技能文件一项）。可为空（`[]`）。                |

### 完整示例：机械臂

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

### 工具命名规范

- 所有工具名称使用 `snake_case`
- 工具名称以设备名称为前缀：`arm_move`、`sensor_read`、`display_show`
- 名称长度不超过 32 个字符
- 避免使用可能与其他工具冲突的通用名称，如 `move` 或 `read`

---

## 6. 数据流架构

```
[物理连接]
  外设固件上电，通过 10kΩ 电阻将 DET 引脚拉高

[GPIO 检测]
  peripheral_detector ISR 在 GPIO 21 上升沿触发
  50ms 去抖定时器启动
  50ms 稳定高电平后：触发已连接回调

[peripheral_manager 接收回调]
  状态：IDLE → HANDSHAKING
  调用 pdp_handshake()：
    uart_write: {"type":"hello","ver":"1"}
    uart_read:  {"type":"ack","name":"robotic_arm",...}
  调用 pdp_request_manifest()：
    uart_write: {"type":"manifest_req"}
    uart_read:  {"type":"manifest",...}
  将清单保存至 /spiffs/peripheral/manifest.json
  遍历每个技能：
    uart_write: {"type":"skill_req","index":N}
    uart_read:  {"type":"skill","content":"..."}
    将内容写入 /spiffs/skills/peripheral_<name>.md
  uart_write: {"type":"ready"}
  uart_read:  {"type":"ready_ack"}
  状态：HANDSHAKING → READY

[工具注册]
  遍历清单中的每个工具：
    tool_peripheral_create_stub(name, description, schema)
    tool_registry_register_dynamic(&stub)
  tool_registry_rebuild_json()
  技能摘要在下次 context_builder 调用时自动包含

[智能体循环接收用户消息]
  context_builder 将 /spiffs/skills/peripheral_robotic_arm.md 包含在系统提示词中
  工具 JSON 现包含 arm_move、arm_grip、arm_home
  LLM 接收到更新后的上下文和工具列表

[tool_use：LLM 调用 arm_move]
  agent_loop 调用 tool_registry_execute("arm_move", input_json, ...)
  tool_registry 分发至外设存根的 execute()
  peripheral_manager_tool_call("arm_move", input_json, output, size)：
    生成 id："a1b2c3d4"
    uart_write: {"type":"tool_call","id":"a1b2c3d4","tool":"arm_move","input":{...}}
    uart_read（10s 超时）: {"type":"tool_result","id":"a1b2c3d4","ok":true,"output":"Moved in 1.2s"}
    将输出字符串返回给智能体

[外设固件]
  解析 tool_call JSON
  执行 arm_move(x, y, z)
  写入结果：{"type":"tool_result","id":"a1b2c3d4","ok":true,"output":"Moved to (100,50,200) in 1.2s"}

[智能体响应]
  工具结果在下次 ReAct 迭代中返回给 LLM
  LLM 生成最终回复："I moved the arm to position (100, 50, 200). It arrived in 1.2 seconds."
  响应推入出站队列 → 路由至 Telegram/WebSocket
```

---

## 7. SPIFFS 存储路径

| 路径                                       | 写入方              | 内容                                |
|--------------------------------------------|---------------------|-------------------------------------|
| `/spiffs/peripheral/manifest.json`         | peripheral_manager  | 当前外设清单（完整 JSON）           |
| `/spiffs/skills/peripheral_<name>.md`      | peripheral_manager  | 外设技能文件（Markdown 格式）       |

外设断开时，`peripheral_manager` 从 SPIFFS 删除清单文件和所有 `peripheral_*` 技能文件，然后从工具注册表中注销外设工具。

断开后的下次 `context_builder` 调用时，技能不再包含在系统提示词中。智能体在下一条消息中将不再感知该外设。

---

## 8. 设计原则

### 第一层始终独立运行

`peripheral_manager_init()` 和 `peripheral_manager_start()` 的失败通过 `ESP_LOGW` 记录并提前返回，绝不使用 `ESP_ERROR_CHECK`。即使以下情况发生，主控脑盒仍能正常启动和运行：

- 未连接任何外设
- UART1 引脚悬空
- 握手超时或收到乱码
- 外设固件在会话中途崩溃

### 无外设连接时零额外开销

`peripheral_manager` 任务阻塞在 FreeRTOS 事件组上，空闲时不消耗任何 CPU 周期。工具注册表中没有动态工具。在检测到连接之前，不分配任何 UART 缓冲区。智能体上下文构建器、LLM 请求和工具执行路径完全不受影响。

### 语音模块编译期禁用

语音硬件是可选的，可能不存在。添加一个 Kconfig 选项：

```kconfig
# main/Kconfig.projbuild
config MIMI_VOICE_ENABLE
    bool "Enable voice I/O (requires INMP441 mic + MAX98357A speaker)"
    default n
    help
        启用 I2S 麦克风采集、STT 转录、
        TTS 合成和扬声器输出。需要：
        - GPIO 4/5/6 上的 INMP441 MEMS 麦克风
        - GPIO 15/16/7 上的 MAX98357A I2S 功放
        - 用于 STT/TTS 的 DashScope 或 OpenAI API 密钥
```

语音源文件通过 `#if CONFIG_MIMI_VOICE_ENABLE` 保护，禁用时从 `CMakeLists.txt` 中排除。这样可以在不需要时避免引入 I2S 驱动代码和 minimp3 库。

### 纯 C / 仅使用 ESP-IDF

所有外设和语音代码均为 C99，仅使用 ESP-IDF API。唯一的外部库例外是 **minimp3**（一个包含在 `main/voice/minimp3.h` 中的公共领域单头文件 MP3 解码器），用于 TTS 音频播放。构建系统无需任何额外修改，仅需将头文件加入源代码树。

### 大缓冲区使用 PSRAM

| 缓冲区                   | 大小    | 位置                               |
|--------------------------|---------|------------------------------------|
| 清单 JSON 接收缓冲区     | 4 KB    | PSRAM（栈分配空间不足）            |
| 技能文件内容缓冲区       | 4 KB    | PSRAM                              |
| 语音录音缓冲区           | 160 KB  | PSRAM                              |
| TTS 音频解码缓冲区       | 64 KB   | PSRAM                              |

所有分配均使用 `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`。主智能体循环的 PSRAM 预算（现有分配后约剩余 7.7 MB）可轻松容纳这些新增分配。

---

## 9. mimi_config.h 常量参考

以下常量为外设和语音子系统新增至 `main/mimi_config.h`。

### 外设 UART 与 GPIO

```c
/* 外设通信的 UART 端口和 GPIO 引脚 */
#define MIMI_PERIPH_UART_PORT        UART_NUM_1
#define MIMI_PERIPH_UART_BAUD        115200
#define MIMI_PERIPH_UART_TX_PIN      17
#define MIMI_PERIPH_UART_RX_PIN      18
#define MIMI_PERIPH_DETECT_PIN       21    /* DET：低电平=断开，高电平=已连接 */

/* UART 缓冲区与时序 */
#define MIMI_PERIPH_UART_BUF_SIZE    (4 * 1024)
#define MIMI_PERIPH_HANDSHAKE_TIMEOUT_MS  5000
#define MIMI_PERIPH_TOOL_TIMEOUT_MS       10000

/* 最大外设工具数（总 MAX_TOOLS=32，减去 9 个内置工具） */
#define MIMI_PERIPH_MAX_TOOLS        23

/* 外设状态的 SPIFFS 路径 */
#define MIMI_PERIPH_DIR              MIMI_SPIFFS_BASE "/peripheral"
#define MIMI_PERIPH_MANIFEST_FILE    MIMI_PERIPH_DIR "/manifest.json"
#define MIMI_PERIPH_SKILLS_PREFIX    MIMI_SPIFFS_BASE "/skills/peripheral_"
```

### 语音 I2S 端口

```c
/* I2S 端口分配 */
#define MIMI_VOICE_I2S_MIC_PORT      I2S_NUM_0
#define MIMI_VOICE_I2S_SPK_PORT      I2S_NUM_1
```

### 麦克风 GPIO（INMP441）

```c
#define MIMI_VOICE_MIC_WS_PIN        4
#define MIMI_VOICE_MIC_SCK_PIN       5
#define MIMI_VOICE_MIC_SD_PIN        6
```

### 扬声器 GPIO（MAX98357A）

```c
#define MIMI_VOICE_SPK_BCLK_PIN      15
#define MIMI_VOICE_SPK_WS_PIN        16
#define MIMI_VOICE_SPK_DIN_PIN       7
```

### 按键与录音参数

```c
/* 按键引脚用于按键通话（BOOT 键 = GPIO0，或外部按键） */
#define MIMI_VOICE_BTN_PIN           0

/* 录音参数 */
#define MIMI_VOICE_SAMPLE_RATE       16000
#define MIMI_VOICE_REC_BUF_SIZE      (160 * 1024)  /* 10s @ 16kHz 16 位单声道 */
#define MIMI_VOICE_VAD_SILENCE_MS    1500
#define MIMI_VOICE_MAX_REC_MS        10000
```

### STT/TTS 服务商与模型

```c
/* STT / TTS 服务商默认值（NVS 键 "voice_provider" 可覆盖） */
#define MIMI_VOICE_STT_PROVIDER      "dashscope"     /* dashscope | openai */
#define MIMI_VOICE_TTS_PROVIDER      "dashscope"
#define MIMI_VOICE_STT_MODEL         "paraformer-realtime-v2"
#define MIMI_VOICE_TTS_MODEL         "cosyvoice-v1"
#define MIMI_VOICE_TTS_VOICE_ID      "longxiaochun"
```

### 语音任务栈大小

```c
#define MIMI_VOICE_INPUT_STACK       (8 * 1024)
#define MIMI_VOICE_OUTPUT_STACK      (8 * 1024)
#define MIMI_VOICE_INPUT_PRIO        4
#define MIMI_VOICE_OUTPUT_PRIO       4
```

### 语音通道与 NVS

```c
/* 语音通道名称（用于 mimi_msg_t.channel） */
#define MIMI_CHAN_VOICE               "voice"

/* 语音配置的 NVS 命名空间 */
#define MIMI_NVS_VOICE               "voice_config"
#define MIMI_NVS_KEY_VOICE_PROVIDER  "provider"
#define MIMI_NVS_KEY_VOICE_KEY       "api_key"
#define MIMI_NVS_KEY_VOICE_MODEL     "model"
```

### 更新后的 FreeRTOS 任务布局

添加外设和语音子系统后：

| 任务                  | 核心 | 优先级 | 栈大小  | 说明                                     |
|-----------------------|------|--------|---------|------------------------------------------|
| `tg_poll`             | 0    | 5      | 12 KB   | Telegram 长轮询                          |
| `agent_loop`          | 1    | 6      | 24 KB   | ReAct 循环 + LLM API 调用                |
| `outbound`            | 0    | 5      | 12 KB   | 将响应路由到各通道                       |
| `cron`                | 任意 | 4      | 4 KB    | 每 60 秒轮询到期的定时任务               |
| `serial_cli`          | 0    | 3      | 4 KB    | USB 串口控制台 REPL                      |
| `peripheral_manager`  | 0    | 4      | 6 KB    | 外设生命周期管理，PDP 握手               |
| `voice_input`         | 0    | 4      | 8 KB    | I2S 麦克风采集 + STT（如已启用）        |
| `voice_output`        | 0    | 4      | 8 KB    | TTS + I2S 扬声器播放（如已启用）        |
| httpd（内部）         | 0    | 5      | —       | WebSocket 服务器                         |
| wifi_event（IDF）     | 0    | 8      | —       | WiFi 事件处理                            |
| heartbeat（定时器）   | —    | —      | —       | FreeRTOS 定时器，每 30 分钟触发一次      |
