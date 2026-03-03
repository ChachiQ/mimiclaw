# MimiClaw 愿景 — 可插拔的 AI 大脑盒子

> **ESP32-S3 + MimiClaw = 即插即用的 AI 大脑。**
> 接上电源，就是完整的 AI 助手。
> 插上机械臂，就能控制硬件。
> 加上麦克风，就能开口说话。

---

## 一句话定义

运行 MimiClaw 的 ESP32-S3 就是一个**可插拔的 AI 大脑盒子**。上电即是完整的 AI 助手；插入外设，LLM 自动理解并驱动它——无需重新编译，无需重新训练。

---

## 问题背景

### 为什么要做这个？

**云端 AI 助手是黑盒子。**
断网就停摆，数据流经你看不见的服务器，你无法检查或修改系统的思维方式。

**边缘 LLM 部署代价高昂。**
在树莓派或 Jetson 上跑一个有能力的 AI 代理，意味着 50 到 200 美元的硬件，一套需要维护的 Linux 系统，以及高到无法电池供电的功耗。这个价位根本无法嵌入普通设备。

**机器人和 IoT 设备的"AI 大脑"与硬件驱动深度耦合。**
换一条机械臂，就得重写固件。大脑应当与硬件解耦——有一个干净的接口，让任何外设描述自身能力，然后被同一个 LLM 代理驱动。

**MimiClaw 的答案：** 一颗 5 美元的 ESP32-S3 芯片就是大脑，任何硬件都可以接上它，LLM 自动理解并驱动所连接的一切。

---

## 三层架构

这是核心设计。三个层次，严格的单向依赖。

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

**核心原则：第一层永远不依赖第二层或第三层。**

第二层和第三层的失败是非致命的。如果 I2S 硬件不存在，语音层跳过初始化，代理继续运行。如果外设在会话中途断开，其工具自动注销，Telegram 照常工作。

### 大脑盒子连接示意图

```
                    ┌──────────────────────┐
  麦克风 ────I2S0───┤                      ├─UART1──► 外设 MCU
                    │    ESP32-S3 大脑      │          （机械臂、
  扬声器 ────I2S1───┤                      │           机器狗、
                    │    MimiClaw 固件      │           传感器板…）
  Telegram / ───────┤                      │
  WebSocket         └──────────────────────┘
```

---

## 大脑与外设的关系

外设设计遵循**声明即驱动**的模型：

1. **外设声明能力** — 外设提供一份 `manifest.json`，描述它的工具（名称、说明、JSON Schema 参数），以及一组教会 LLM 如何使用这些工具的技能文件（`.md` 格式）。
2. **大脑通过 PDP 协议握手** — 检测到物理连接（GPIO21 触发）后，大脑通过 UART1 发起 PDP v1 握手，读取清单文件，把技能文件传输到 SPIFFS，并将每个工具注册进运行时的工具注册表。
3. **LLM 用自然语言驱动任何外设** — 新注册的工具出现在下一次 API 调用的工具列表中，无需重新编译。LLM 自行推理应该调用哪些工具、以什么顺序调用。
4. **断开连接时工具自动注销** — 大脑从注册表中移除外设工具，然后继续正常运行，既不崩溃，也不重启。

这意味着同一颗 ESP32-S3 大脑，今天可以驱动机械臂，明天可以驱动传感器阵列。LLM 不需要重新训练，外设只需要换一份不同的自我描述。

---

## 端到端示例

> "把桌上那个红色杯子移到右边。"

完整的执行路径如下：

```
用户说话 ──► INMP441 麦克风（I2S0）
          ──► 能量 VAD 检测到语音
          ──► DashScope STT → 文本字符串
          ──► Agent ReAct 循环开始
               │
               ├─ 工具调用 1: arm_status()           → "就绪，位置 (0,0,0)"
               ├─ 工具调用 2: arm_move(x=120, y=50)  → "已移动"
               ├─ 工具调用 3: arm_gripper(open=true)  → "已张开"
               ├─ 工具调用 4: arm_move(x=240, y=50)  → "已移动"
               └─ 工具调用 5: arm_gripper(open=false) → "已夹住"
               │
          ──► 代理生成回复："好的，杯子已经移到右边了。"
          ──► DashScope TTS → PCM 音频
          ──► MAX98357A 扬声器（I2S1）播放音频
```

五次工具调用，全部通过 UART1 串行发送给外设 MCU。LLM 从一条自然语言指令出发，自行规划了整个执行序列。

---

## 设计哲学

六条原则指导着 MimiClaw 的每一个架构决策。

### 1. 始终独立

大脑核心在没有任何外部硬件或网络的情况下照常工作。WiFi 断开时，串口 CLI 仍然可用。本地工具（`read_file`、`write_file`、`get_current_time`）无需云端即可运行。MimiClaw 不是一个傻瓜 HTTP 代理，它是一个自给自足的 AI 代理。

### 2. 优雅降级

- WiFi 断开 → CLI 和本地工具保持可用
- 外设断开 → Telegram 和 WebSocket 继续运行，外设工具静默注销
- 没有麦克风 → 语音层跳过初始化，其余正常运行
- LLM API 不可达 → 代理返回错误消息，不崩溃，不死锁

故障被隔离在各层之内，任何一层都无法拖垮下面的层。

### 3. 零厂商锁定

- **LLM**：可在运行时通过 `mimi_secrets.h` 或 NVS CLI 在 Anthropic（Claude）和 OpenAI（GPT）之间切换
- **STT/TTS**：可在 DashScope 和 Whisper 等其他提供商之间切换
- **外设 MCU**：任何能说 UART、实现 PDP v1 的微控制器都行——Arduino、STM32、RP2040、裸 AVR
- **接入渠道**：今天是 Telegram 和 WebSocket，消息总线可接受新的渠道驱动，无需修改代理

### 4. 人类可读的存储

所有持久化状态都是 SPIFFS 上的纯文本文件：

| 文件 | 用途 |
|------|------|
| `config/SOUL.md` | 代理人格 |
| `config/USER.md` | 用户档案 |
| `memory/MEMORY.md` | 长期记忆 |
| `memory/YYYY-MM-DD.md` | 每日笔记 |
| `sessions/tg_<id>.jsonl` | 每个对话的历史 |
| `cron/cron.json` | 定时任务 |
| `skills/peripheral_<name>.md` | 外设技能文件 |

开发者挂载 SPIFFS 分区，用任何文本编辑器就能读懂代理知道什么、记住什么。没有二进制 blob，没有不透明的数据库。

### 5. 技能文件是知识的载体

外设能力的 `.md` 技能文件在运行时注入到系统提示中。LLM 读取后立即知道如何使用新硬件——这些工具的作用、何时调用、接受什么参数、会产生什么副作用。无需重新编译，无需重新训练。换一份技能文件，就能换一套行为。

### 6. 最低硬件成本

| 组件 | 目标成本 |
|------|---------|
| ESP32-S3 模块 | ~$5 |
| INMP441 麦克风 | ~$1 |
| MAX98357A 功放 + 扬声器 | ~$3 |
| 外壳 + 电源 | ~$5 |
| **完整语音盒子** | **< $15** |

外设 MCU 是独立的，可以便宜到一颗 2 美元的 Arduino Nano。大脑本身不需要昂贵的计算硬件。

---

## 路线图

### 已完成

- ✅ **Layer 1**：完整 ReAct 代理、Telegram 长轮询、WebSocket 网关、9 个内置工具（`web_search`、`get_current_time`、`read_file`、`write_file`、`edit_file`、`list_dir`、`cron_add`、`cron_list`、`cron_remove`）、SPIFFS 记忆、定时调度器、心跳、技能加载器
- ✅ **Layer 2**：语音框架（`voice_input`、`voice_output`、`voice_channel`）、DashScope STT/TTS、I2S 硬件驱动、基于能量的 VAD、PTT 按钮
- ✅ **Layer 3**：PDP v1 协议、外设热插拔检测（GPIO21）、动态工具注册（`tool_registry_register_dynamic`）、断开时自动注销、技能文件传输到 SPIFFS

### 规划中

- 🔲 **ESP-SR 离线唤醒词** — "Hey Mimi" 无需按键即可触发监听，无需联网
- 🔲 **参考外设固件** — Arduino 机械臂 demo，包含完整 PDP v1 实现，可作为外设 SDK 模板
- 🔲 **多外设支持** — UART2 作为第二路外设接口，允许两个独立外设同时在线
- 🔲 **外设安全验证** — 清单文件签名，防止不受信任的外设注入恶意工具定义

---

## 文档导航

从这里出发，根据你的目标选择路径：

| 文档 | 读者 | 内容 |
|------|------|------|
| **VISION_CN.md**（本文） | 所有人 | MimiClaw 存在的原因、做什么、设计哲学 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 开发者 | 模块地图、双核设计、队列架构、内存布局 |
| [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) | 开发者 | 构建环境、添加工具、添加接入渠道、调试方法 |
| [PERIPHERAL_DESIGN.md](PERIPHERAL_DESIGN.md) | 硬件开发者 | PDP v1 协议规范、清单文件格式、技能文件格式 |
| [PERIPHERAL_SDK.md](PERIPHERAL_SDK.md) | 外设固件作者 | 实现 PDP v1 外设的分步指南 |

想**理解整个系统**，读 ARCHITECTURE.md。
想**在它上面开发**，读 DEVELOPER_GUIDE.md。
想**接入硬件外设**，读 PERIPHERAL_DESIGN.md 和 PERIPHERAL_SDK.md。
