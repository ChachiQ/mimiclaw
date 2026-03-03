# MimiClaw: 可插拔 AI 大脑盒子

<p align="center">
  <img src="assets/banner.png" alt="MimiClaw" width="500" />
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
  <a href="https://deepwiki.com/memovai/mimiclaw"><img src="https://img.shields.io/badge/DeepWiki-mimiclaw-blue.svg" alt="DeepWiki"></a>
  <a href="https://discord.gg/r8ZxSvB8Yr"><img src="https://img.shields.io/badge/Discord-mimiclaw-5865F2?logo=discord&logoColor=white" alt="Discord"></a>
  <a href="https://x.com/ssslvky"><img src="https://img.shields.io/badge/X-@ssslvky-black?logo=x" alt="X"></a>
</p>

<p align="center">
  <strong><a href="README.md">English</a> | <a href="README_CN.md">中文</a> | <a href="README_JA.md">日本語</a></strong>
</p>

**ESP32-S3 + MimiClaw = 可插拔 AI 大脑盒子。上电即是完整 AI 助理，插上外设 LLM 自动驱动它。**

MimiClaw 是一个模块化 AI 智能体固件，运行在 ESP32-S3 上。核心是一个 24/7 运行的 ReAct LLM 智能体 — 没有 Linux，没有 Node.js，纯 C + FreeRTOS，跑在一颗 $5 的芯片上。接上麦克风和喇叭即可语音交互，通过 UART 连接任意 MCU 后 AI 立即获得驱动它的新工具。

## 三层架构

```
┌─────────────────────────────────────────────────────┐
│  第三层：外设能力（热插拔，可选）                         │
│  插入机械臂 → +arm_move / +arm_gripper               │
│  插入机器狗 → +walk / +turn / +grip                  │
├─────────────────────────────────────────────────────┤
│  第二层：语音 I/O（编译开关，可选）                        │
│  有麦克风+喇叭 → 语音输入/输出                           │
│  无硬件 → 仅 Telegram / WebSocket                    │
├─────────────────────────────────────────────────────┤
│  第一层：大脑核心（始终独立运行）                           │
│  LLM 智能体 + 工具 + 记忆 + Telegram + WebSocket      │
└─────────────────────────────────────────────────────┘
```

**关键不变量：第一层永远不依赖第二层或第三层。拔掉外设 — Telegram 照常工作。关闭语音 — 其余一切照常。**

## MimiClaw 能做什么

- **大脑核心（始终运行）** — ReAct LLM 智能体循环、9 个内置工具、持久化 SPIFFS 记忆、Telegram 轮询、WebSocket 网关、Cron 调度器、心跳服务
- **语音 I/O（可选）** — I2S INMP441 麦克风 + MAX98357A 喇叭，DashScope STT/TTS，通过编译开关启用；初始化失败不影响整体运行
- **外设热插拔（可选）** — 任意 MCU 通过 UART1（GPIO17/18）接入，发送 Manifest JSON 声明能力，能力自动通过 PDP v1 协议注册为 LLM 工具
- **小巧自洽** — 没有 Linux，没有 Node.js，没有臃肿依赖 — 纯 C 跑在 $5 芯片上
- **双提供商** — 同时支持 Anthropic (Claude) 和 OpenAI (GPT)，运行时可切换

## 工作原理

![](assets/mimiclaw.png)

消息通过 Telegram 或 WebSocket 到达，ESP32-S3 在 Core 0 上接收后推入 inbound 队列，Core 1 上的 ReAct 智能体循环接管 — LLM 思考、调用工具、读取记忆 — 再把回复发回去。如果外设已连接，其工具与内置工具一起对 LLM 可用。如果语音已启用，语音输入会先转录再进入循环，回复也会合成为语音输出。一切都跑在一颗芯片上，所有数据存储在本地 Flash。

## 快速开始

### 你需要

- 一块 **ESP32-S3 开发板**，16MB Flash + 8MB PSRAM（如小智 AI 开发板，~¥30）
- 一根 **USB Type-C 数据线**
- 一个 **Telegram Bot Token** — 在 Telegram 找 [@BotFather](https://t.me/BotFather) 创建
- 一个 **Anthropic API Key** — 从 [console.anthropic.com](https://console.anthropic.com) 获取，或一个 **OpenAI API Key** — 从 [platform.openai.com](https://platform.openai.com) 获取

### 安装

```bash
# 需要先安装 ESP-IDF v5.5+:
# https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/get-started/

git clone https://github.com/memovai/mimiclaw.git
cd mimiclaw

idf.py set-target esp32s3
```

<details>
<summary>Ubuntu 安装</summary>

建议基线：

- Ubuntu 22.04/24.04
- Python >= 3.10
- CMake >= 3.16
- Ninja >= 1.10
- Git >= 2.34
- flex >= 2.6
- bison >= 3.8
- gperf >= 3.1
- dfu-util >= 0.11
- `libusb-1.0-0`、`libffi-dev`、`libssl-dev`

Ubuntu 安装与构建：

```bash
sudo apt-get update
sudo apt-get install -y git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

./scripts/setup_idf_ubuntu.sh
./scripts/build_ubuntu.sh
```

</details>

<details>
<summary>macOS 安装</summary>

建议基线：

- macOS 12/13/14
- Xcode Command Line Tools
- Homebrew
- Python >= 3.10
- CMake >= 3.16
- Ninja >= 1.10
- Git >= 2.34
- flex >= 2.6
- bison >= 3.8
- gperf >= 3.1
- dfu-util >= 0.11
- `libusb`、`libffi`、`openssl`

macOS 安装与构建：

```bash
xcode-select --install
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

./scripts/setup_idf_macos.sh
./scripts/build_macos.sh
```

</details>

### 配置

MimiClaw 使用**两层配置**：`mimi_secrets.h` 提供编译时默认值，串口 CLI 可在运行时覆盖。CLI 设置的值存在 NVS Flash 中，优先级高于编译时值。

```bash
cp main/mimi_secrets.h.example main/mimi_secrets.h
```

编辑 `main/mimi_secrets.h`：

```c
#define MIMI_SECRET_WIFI_SSID       "你的WiFi名"
#define MIMI_SECRET_WIFI_PASS       "你的WiFi密码"
#define MIMI_SECRET_TG_TOKEN        "123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11"
#define MIMI_SECRET_API_KEY         "sk-ant-api03-xxxxx"
#define MIMI_SECRET_MODEL_PROVIDER  "anthropic"     // "anthropic" 或 "openai"
#define MIMI_SECRET_SEARCH_KEY      ""              // 可选：Brave Search API key
#define MIMI_SECRET_PROXY_HOST      "10.0.0.1"      // 可选：代理地址
#define MIMI_SECRET_PROXY_PORT      "7897"           // 可选：代理端口
```

然后编译烧录：

```bash
# 完整编译（修改 mimi_secrets.h 后必须 fullclean）
idf.py fullclean && idf.py build

# 查找串口
ls /dev/cu.usb*          # macOS
ls /dev/ttyACM*          # Linux

# 烧录并监控（将 PORT 替换为你的串口）
# USB 转接器：大概率是 /dev/cu.usbmodem11401（macOS）或 /dev/ttyACM0（Linux）
idf.py -p PORT flash monitor
```

> **注意：请插对 USB 口！** 大多数 ESP32-S3 开发板有两个 Type-C 接口，必须插标有 **USB** 的那个口（原生 USB Serial/JTAG），**不要**插标有 **COM** 的口（外部 UART 桥接）。插错口会导致烧录/监控失败。
>
> <details>
> <summary>查看参考图片</summary>
>
> <img src="assets/esp32s3-usb-port.jpg" alt="请插 USB 口，不要插 COM 口" width="480" />
>
> </details>

### 代理配置（国内用户）

在国内需要代理才能访问 Telegram 和 Anthropic API。MimiClaw 内置 HTTP CONNECT 隧道支持。

**前提**：局域网内有一个支持 HTTP CONNECT 的代理（Clash Verge、V2Ray 等），并开启了「允许局域网连接」。

可以在 `mimi_secrets.h` 中编译时设置，也可以通过串口 CLI 随时修改：

```
mimi> set_proxy 192.168.1.83 7897   # 设置代理
mimi> clear_proxy                    # 清除代理
```

> **提示**：确保 ESP32-S3 和代理机器在同一局域网。Clash Verge 在「设置 → 允许局域网」中开启。

### CLI 命令（通过 UART/COM 口连接）

通过串口连接即可配置和调试。**配置命令**让你无需重新编译就能修改设置 — 随时随地插上 USB 线就能改。

**运行时配置**（存入 NVS，覆盖编译时默认值）：

```
mimi> wifi_set MySSID MyPassword   # 换 WiFi
mimi> set_tg_token 123456:ABC...   # 换 Telegram Bot Token
mimi> set_api_key sk-ant-api03-... # 换 API Key（Anthropic 或 OpenAI）
mimi> set_model_provider openai    # 切换提供商（anthropic|openai）
mimi> set_model gpt-4o             # 换模型
mimi> set_proxy 192.168.1.83 7897  # 设置代理
mimi> clear_proxy                  # 清除代理
mimi> set_search_key BSA...        # 设置 Brave Search API Key
mimi> config_show                  # 查看所有配置（脱敏显示）
mimi> config_reset                 # 清除 NVS，恢复编译时默认值
```

**调试与运维：**

```
mimi> wifi_status              # 连上了吗？
mimi> memory_read              # 看看它记住了什么
mimi> memory_write "内容"       # 写入 MEMORY.md
mimi> heap_info                # 还剩多少内存？
mimi> session_list             # 列出所有会话
mimi> session_clear 12345      # 删除一个会话
mimi> heartbeat_trigger           # 手动触发一次心跳检查
mimi> cron_start                  # 立即启动 cron 调度器
mimi> restart                     # 重启
```

### USB (JTAG) 与 UART：哪个口做什么

大多数 ESP32-S3 开发板有 **两个 USB-C 口**：

| 端口 | 用途 |
|------|------|
| **USB**（JTAG） | `idf.py flash`、JTAG 调试 |
| **COM**（UART） | **REPL 命令行**、串口控制台 |

> **REPL 必须连接 UART（COM）口。** USB（JTAG）口不支持交互式 REPL 输入。

<details>
<summary>端口详情与推荐工作流</summary>

| 端口 | 标注 | 协议 |
|------|------|------|
| **USB** | USB / JTAG | 原生 USB Serial/JTAG |
| **COM** | UART / COM | 外置 UART 桥接芯片（CP2102/CH340） |

ESP-IDF 控制台默认配置为 UART 输出（`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`）。

**同时连接两个口时：**

- USB（JTAG）口负责烧录/下载，并提供辅助串口输出
- UART（COM）口提供主要的交互式控制台，用于 REPL
- macOS 下两个口都会显示为 `/dev/cu.usbmodem*` 或 `/dev/cu.usbserial-*`，用 `ls /dev/cu.usb*` 区分
- Linux 下 USB（JTAG）通常是 `/dev/ttyACM0`，UART 通常是 `/dev/ttyUSB0`

**推荐工作流：**

```bash
# 通过 USB（JTAG）口烧录
idf.py -p /dev/cu.usbmodem11401 flash

# 通过 UART（COM）口打开 REPL
idf.py -p /dev/cu.usbserial-110 monitor
# 或使用任意串口工具：screen、minicom、PuTTY，波特率 115200
```

</details>

## 记忆

MimiClaw 把所有数据存为纯文本文件，可以直接读取和编辑：

| 文件 | 说明 |
|------|------|
| `SOUL.md` | 机器人的人设 — 编辑它来改变行为方式 |
| `USER.md` | 关于你的信息 — 姓名、偏好、语言 |
| `MEMORY.md` | 长期记忆 — 它应该一直记住的事 |
| `HEARTBEAT.md` | 待办清单 — 机器人定期检查并自主执行 |
| `cron.json` | 定时任务 — AI 创建的周期性或一次性任务 |
| `2026-02-05.md` | 每日笔记 — 今天发生了什么 |
| `tg_12345.jsonl` | 聊天记录 — 你和它的对话 |

## 工具

MimiClaw 同时支持 Anthropic 和 OpenAI 的工具调用 — LLM 在对话中可以调用工具，循环执行直到任务完成（ReAct 模式）。外设工具在外设连接时动态注册。

| 工具 | 说明 |
|------|------|
| `web_search` | 通过 Brave Search API 搜索网页，获取实时信息 |
| `get_current_time` | 通过 HTTP 获取当前日期和时间，并设置系统时钟 |
| `read_file` | 从 SPIFFS Flash 读取文件 |
| `write_file` | 向 SPIFFS Flash 写入文件 |
| `edit_file` | 编辑 SPIFFS Flash 上的已有文件 |
| `list_dir` | 列出 SPIFFS 目录下的文件 |
| `cron_add` | 创建定时或一次性任务（LLM 自主创建 cron 任务） |
| `cron_list` | 列出所有已调度的 cron 任务 |
| `cron_remove` | 按 ID 删除 cron 任务 |

启用网页搜索需要在 `mimi_secrets.h` 中设置 [Brave Search API key](https://brave.com/search/api/)（`MIMI_SECRET_SEARCH_KEY`）。

## 定时任务（Cron）

MimiClaw 内置 cron 调度器，让 AI 可以自主安排任务。LLM 可以通过 `cron_add` 工具创建周期性任务（"每 N 秒"）或一次性任务（"在某个时间戳"）。任务触发时，消息会注入到 Agent 循环 — AI 自动醒来、处理任务并回复。

任务持久化存储在 SPIFFS（`cron.json`），重启后不会丢失。典型用途：每日总结、定时提醒、定期巡检。

## 心跳（Heartbeat）

心跳服务会定期读取 SPIFFS 上的 `HEARTBEAT.md`，检查是否有待办事项。如果发现未完成的条目（非空行、非标题、非已勾选的 `- [x]`），就会向 Agent 循环发送提示，让 AI 自主处理。

这让 MimiClaw 变成一个主动型助理 — 把任务写入 `HEARTBEAT.md`，机器人会在下一次心跳周期自动拾取执行（默认每 30 分钟）。

## 其他功能

- **WebSocket 网关** — 端口 18789，局域网内用任意 WebSocket 客户端连接
- **OTA 更新** — WiFi 远程刷固件，无需 USB
- **双核** — 网络 I/O 和 AI 处理分别跑在不同 CPU 核心
- **HTTP 代理** — CONNECT 隧道，适配受限网络
- **多提供商** — 同时支持 Anthropic (Claude) 和 OpenAI (GPT)，运行时可切换
- **定时任务** — AI 可自主创建周期性和一次性任务，重启后持久保存
- **心跳服务** — 定期检查任务文件，驱动 AI 自主执行
- **工具调用** — ReAct Agent 循环，两种提供商均支持工具调用
- **语音 I/O（第二层）** — 可选 I2S 麦克风（INMP441）和喇叭（MAX98357A），DashScope STT/TTS；通过编译开关启用，硬件缺失时非致命
- **外设热插拔（第三层）** — 通过 UART1 连接任意 MCU；外设通过 Manifest JSON 和 Skill .md 文件声明能力，自动注册为 LLM 可调用工具（PDP v1 协议）；断开连接后工具干净卸载

## 开发者

技术细节在 `docs/` 文件夹：

- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — 系统设计、模块划分、任务布局、内存分配、协议、Flash 分区
- **[docs/VISION.md](docs/VISION.md)** — 项目愿景与长期设计哲学
- **[docs/PERIPHERAL_DESIGN.md](docs/PERIPHERAL_DESIGN.md)** — PDP v1 协议规范、外设 Manifest 格式、工具注册生命周期
- **[docs/TODO.md](docs/TODO.md)** — 功能差距和路线图

## 贡献

提交 Issue 或 Pull Request 前，请先阅读 **[CONTRIBUTING.md](CONTRIBUTING.md)**。

## 贡献者

感谢所有为 MimiClaw 做出贡献的开发者。

<a href="https://github.com/memovai/mimiclaw/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=memovai/mimiclaw" alt="MimiClaw contributors" />
</a>

## 许可证

MIT

## 致谢

灵感来自 [OpenClaw](https://github.com/openclaw/openclaw) 和 [Nanobot](https://github.com/HKUDS/nanobot)。MimiClaw 为嵌入式硬件重新实现了核心 AI Agent 架构 — 没有 Linux，没有服务器，只有一颗 $5 的芯片。

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=memovai/mimiclaw&type=Date)](https://star-history.com/#memovai/mimiclaw&Date)
