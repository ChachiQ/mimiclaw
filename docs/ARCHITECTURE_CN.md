# MimiClaw 架构文档

> ESP32-S3 AI 智能体固件 —— 基于 C/FreeRTOS 的裸机实现（无 Linux）。

---

## 系统概览

```
Telegram App (User)
    │
    │  HTTPS Long Polling
    │
    ▼
┌──────────────────────────────────────────────────┐
│               ESP32-S3 (MimiClaw)                │
│                                                  │
│   ┌─────────────┐       ┌──────────────────┐     │
│   │  Telegram    │──────▶│   Inbound Queue  │     │
│   │  Poller      │       └────────┬─────────┘     │
│   │  (Core 0)    │               │                │
│   └─────────────┘               ▼                │
│                     ┌────────────────────────┐    │
│   ┌─────────────┐  │     Agent Loop          │    │
│   │  WebSocket   │─▶│     (Core 1)           │    │
│   │  Server      │  │                        │    │
│   │  (:18789)    │  │  Context ──▶ LLM Proxy │    │
│   └─────────────┘  │  Builder      (HTTPS)   │    │
│                     │       ▲          │      │    │
│   ┌─────────────┐  │       │     tool_use?   │    │
│   │  Serial CLI  │  │       │          ▼      │    │
│   │  (Core 0)    │  │  Tool Results ◀─ Tools  │    │
│   └─────────────┘  │              (web_search)│    │
│                     └──────────┬─────────────┘    │
│                                │                  │
│                         ┌──────▼───────┐          │
│                         │ Outbound Queue│          │
│                         └──────┬───────┘          │
│                                │                  │
│                         ┌──────▼───────┐          │
│                         │  Outbound    │          │
│                         │  Dispatch    │          │
│                         │  (Core 0)    │          │
│                         └──┬────────┬──┘          │
│                            │        │             │
│                     Telegram    WebSocket          │
│                     sendMessage  send              │
│                                                   │
│   ┌──────────────────────────────────────────┐    │
│   │  SPIFFS (12 MB)                          │    │
│   │  /spiffs/config/  SOUL.md, USER.md       │    │
│   │  /spiffs/memory/  MEMORY.md, YYYY-MM-DD  │    │
│   │  /spiffs/sessions/ tg_<chat_id>.jsonl    │    │
│   └──────────────────────────────────────────┘    │
└───────────────────────────────────────────────────┘
         │
         │  Anthropic Messages API (HTTPS)
         │  + Brave Search API (HTTPS)
         ▼
   ┌───────────┐   ┌──────────────┐
   │ Claude API │   │ Brave Search │
   └───────────┘   └──────────────┘
```

---

## 数据流

```
1. 用户在 Telegram（或 WebSocket）发送消息
2. 信道轮询器接收消息，封装为 mimi_msg_t
3. 消息推入入站队列（FreeRTOS xQueue）
4. 智能体循环（Core 1）弹出消息：
   a. 从 SPIFFS 加载会话历史（JSONL 格式）
   b. 构建系统提示词（SOUL.md + USER.md + MEMORY.md + 近期日记 + 工具引导）
   c. 构建 cJSON 消息数组（历史记录 + 当前消息）
   d. ReAct 循环（最多 10 次迭代）：
      i.   通过 HTTPS 调用 Claude API（非流式，携带 tools 数组）
      ii.  解析 JSON 响应 → 文本块 + tool_use 块
      iii. 若 stop_reason == "tool_use"：
           - 执行各工具（例如 web_search → Brave Search API）
           - 将 assistant 内容 + tool_result 追加到消息中
           - 继续循环
      iv.  若 stop_reason == "end_turn"：以最终文本退出循环
   e. 将用户消息 + 最终助手回复保存到会话文件
   f. 将响应推入出站队列
5. 出站调度器（Core 0）弹出响应：
   a. 按 channel 字段路由（"telegram" → sendMessage，"websocket" → WS 帧）
6. 用户收到回复
```

---

## 模块结构

```
main/
├── mimi.c                  入口点 —— app_main() 负责编排初始化与启动流程
├── mimi_config.h           所有编译期常量 + 编译期密钥头文件引用
├── mimi_secrets.h          编译期凭据（已加入 gitignore，优先级最高）
├── mimi_secrets.h.example  mimi_secrets.h 的模板文件
│
├── bus/
│   ├── message_bus.h       mimi_msg_t 结构体，队列 API
│   └── message_bus.c       两个 FreeRTOS 队列：入站 + 出站
│
├── wifi/
│   ├── wifi_manager.h      WiFi STA 生命周期 API
│   └── wifi_manager.c      事件处理器，指数退避重连
│
├── telegram/
│   ├── telegram_bot.h      Bot 初始化/启动，send_message API
│   └── telegram_bot.c      长轮询循环，JSON 解析，消息分片
│
├── llm/
│   ├── llm_proxy.h         llm_chat() + llm_chat_tools() API，tool_use 类型定义
│   └── llm_proxy.c         Anthropic/OpenAI API（非流式），tool_use 解析
│
├── agent/
│   ├── agent_loop.h        智能体任务初始化/启动
│   ├── agent_loop.c        ReAct 循环：LLM 调用 → 工具执行 → 重复
│   ├── context_builder.h   系统提示词 + 消息构建器 API
│   └── context_builder.c   读取引导文件 + 记忆 + 技能摘要 + 工具引导
│
├── tools/
│   ├── tool_registry.h     工具定义结构体，注册/派发 API
│   ├── tool_registry.c     工具注册，JSON Schema 构建，按名称派发
│   ├── tool_web_search.h/c 通过 HTTPS 调用 Brave Search API（直连 + 代理）
│   ├── tool_get_time.h/c   NTP 同步的当前时间
│   ├── tool_files.h/c      SPIFFS 文件读写（read_file, write_file, edit_file, list_dir）
│   └── tool_cron.h/c       Cron 任务管理（cron_add, cron_list, cron_remove）
│
├── memory/
│   ├── memory_store.h      长期记忆 + 每日记忆 API
│   ├── memory_store.c      MEMORY.md 读写，每日 .md 追加/读取
│   ├── session_mgr.h       单会话 API
│   └── session_mgr.c       JSONL 会话文件，环形缓冲区历史记录
│
├── gateway/
│   ├── ws_server.h         WebSocket 服务器 API
│   └── ws_server.c         支持 WS 升级的 ESP HTTP 服务器，客户端跟踪
│
├── proxy/
│   ├── http_proxy.h        代理连接 API
│   └── http_proxy.c        HTTP CONNECT 隧道 + 通过 esp_tls 建立 TLS
│
├── cli/
│   ├── serial_cli.h        CLI 初始化 API
│   └── serial_cli.c        基于 esp_console 的 REPL，含配置、调试与维护命令
│
├── cron/
│   ├── cron_service.h      Cron 调度器 API（init/start/add/remove/list）
│   └── cron_service.c      持久化任务存储（cron.json），FreeRTOS 任务，入站注入
│
├── heartbeat/
│   ├── heartbeat.h         心跳 API（init/start/trigger）
│   └── heartbeat.c         FreeRTOS 定时器（30 分钟），读取 HEARTBEAT.md，触发智能体
│
├── skills/
│   ├── skill_loader.h      技能加载器 API（init/build_summary）
│   └── skill_loader.c      从 /spiffs/skills/ 加载 .md 文件，注入系统提示词
│
└── ota/
    ├── ota_manager.h       OTA 更新 API
    └── ota_manager.c       esp_https_ota 封装
```

---

## FreeRTOS 任务布局

| 任务               | 核心 | 优先级 | 栈大小 | 描述                                   |
|--------------------|------|--------|--------|----------------------------------------|
| `tg_poll`          | 0    | 5      | 12 KB  | Telegram 长轮询（30 秒超时）           |
| `agent_loop`       | 1    | 6      | 24 KB  | 消息处理 + LLM API 调用                |
| `outbound`         | 0    | 5      | 12 KB  | 将响应路由至 Telegram / WS             |
| `cron`             | 任意 | 4      | 4 KB   | 每 60 秒轮询到期的 Cron 任务           |
| `serial_cli`       | 0    | 3      | 4 KB   | USB 串口控制台 REPL                    |
| httpd（内部）      | 0    | 5      | —      | WebSocket 服务器（esp_http_server）    |
| wifi_event（IDF）  | 0    | 8      | —      | WiFi 事件处理（ESP-IDF）               |
| heartbeat（定时器）| —    | —      | —      | FreeRTOS 定时器，每 30 分钟触发一次    |

**核心分配策略**：Core 0 负责 I/O（网络、串口、WiFi）；Core 1 专用于智能体循环（CPU 密集型 JSON 构建 + HTTPS 等待）。

---

## 内存预算

| 用途                                   | 位置           | 大小     |
|----------------------------------------|----------------|----------|
| FreeRTOS 任务栈                        | 内部 SRAM      | ~40 KB   |
| WiFi 缓冲区                            | 内部 SRAM      | ~30 KB   |
| TLS 连接 x2（Telegram + Claude）       | PSRAM          | ~120 KB  |
| JSON 解析缓冲区                        | PSRAM          | ~32 KB   |
| 会话历史缓存                           | PSRAM          | ~32 KB   |
| 系统提示词缓冲区                       | PSRAM          | ~16 KB   |
| LLM 响应流缓冲区                       | PSRAM          | ~32 KB   |
| 剩余可用                               | PSRAM          | ~7.7 MB  |

32 KB 及以上的大缓冲区通过 `heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM)` 从 PSRAM 分配。

---

## Flash 分区布局

```
Offset      Size      Name        Purpose
─────────────────────────────────────────────
0x009000    24 KB     nvs         ESP-IDF internal use (WiFi calibration etc.)
0x00F000     8 KB     otadata     OTA boot state
0x011000     4 KB     phy_init    WiFi PHY calibration
0x020000     2 MB     ota_0       Firmware slot A
0x220000     2 MB     ota_1       Firmware slot B
0x420000    12 MB     spiffs      Markdown memory, sessions, config
0xFF0000    64 KB     coredump    Crash dump storage
```

Flash 总容量：16 MB。

---

## 存储布局（SPIFFS）

SPIFFS 是一个扁平文件系统，不存在真实目录，文件使用类路径名称表示。

```
/spiffs/config/SOUL.md          AI personality definition
/spiffs/config/USER.md          User profile
/spiffs/memory/MEMORY.md        Long-term persistent memory
/spiffs/memory/2026-02-05.md    Daily notes (one file per day)
/spiffs/sessions/tg_12345.jsonl Session history (one file per Telegram chat)
/spiffs/cron.json               Persisted cron job definitions
/spiffs/HEARTBEAT.md            Pending tasks checked every 30 minutes
/spiffs/skills/weather.md       Built-in skill (loaded on first boot)
/spiffs/skills/daily_briefing.md Built-in skill
/spiffs/skills/skill_creator.md  Built-in skill
/spiffs/skills/<custom>.md      Custom skills added by the agent or user
```

会话文件为 JSONL 格式（每行一个 JSON 对象）：
```json
{"role":"user","content":"Hello","ts":1738764800}
{"role":"assistant","content":"Hi there!","ts":1738764802}
```

---

## 配置

MimiClaw 采用**双层配置系统**：

1. **编译期密钥**（`mimi_secrets.h`）：优先级最高，烧录进固件
2. **运行时 NVS 覆盖**：通过 CLI 命令设置（`set_wifi`、`set_api_key` 等），重启后保留

运行时，各子系统优先读取 NVS；若对应 NVS 键不存在，则回退到编译期值。使用 `config_show` 查看当前生效配置，使用 `config_reset` 清除所有 NVS 覆盖并恢复编译期默认值。

**编译期宏定义**（`mimi_secrets.h`）：

| 宏定义                          | 说明                                        |
|---------------------------------|---------------------------------------------|
| `MIMI_SECRET_WIFI_SSID`        | WiFi SSID                                   |
| `MIMI_SECRET_WIFI_PASS`        | WiFi 密码                                   |
| `MIMI_SECRET_TG_TOKEN`         | Telegram Bot API 令牌                       |
| `MIMI_SECRET_API_KEY`          | LLM API 密钥（Anthropic 或 OpenAI）         |
| `MIMI_SECRET_MODEL`            | 模型 ID（默认：claude-opus-4-5）            |
| `MIMI_SECRET_MODEL_PROVIDER`   | `anthropic` 或 `openai`（默认：anthropic）  |
| `MIMI_SECRET_PROXY_HOST`       | HTTP/SOCKS5 代理主机名/IP（可选）           |
| `MIMI_SECRET_PROXY_PORT`       | 代理端口（可选）                            |
| `MIMI_SECRET_PROXY_TYPE`       | `http` 或 `socks5`（可选）                  |
| `MIMI_SECRET_SEARCH_KEY`       | Brave Search API 密钥（可选）               |

**NVS 命名空间**（与编译期宏定义对应）：
`wifi_config`, `tg_config`, `llm_config`, `proxy_config`, `search_config`

---

## 消息总线协议

内部消息总线使用两个 FreeRTOS 队列，传输 `mimi_msg_t` 消息：

```c
typedef struct {
    char channel[16];   // "telegram", "websocket", "cli"
    char chat_id[32];   // Telegram chat ID or WS client ID
    char *content;      // Heap-allocated text (ownership transferred)
} mimi_msg_t;
```

- **入站队列**：信道 → 智能体循环（队列深度：8）
- **出站队列**：智能体循环 → 调度器 → 信道（队列深度：8）
- 内容字符串的所有权在推入时转移，接收方必须调用 `free()` 释放。

---

## WebSocket 协议

端口：**18789**。最大客户端数：**4**。

**客户端 → 服务端：**
```json
{"type": "message", "content": "Hello", "chat_id": "ws_client1"}
```

**服务端 → 客户端：**
```json
{"type": "response", "content": "Hi there!", "chat_id": "ws_client1"}
```

客户端的 `chat_id` 在连接时自动分配（`ws_<fd>`），可在第一条消息中覆盖。

---

## Claude API 集成

接口端点：`POST https://api.anthropic.com/v1/messages`

请求格式（Anthropic 原生格式，非流式，携带工具定义）：
```json
{
  "model": "claude-opus-4-6",
  "max_tokens": 4096,
  "system": "<system prompt>",
  "tools": [
    {
      "name": "web_search",
      "description": "Search the web for current information.",
      "input_schema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}
    }
  ],
  "messages": [
    {"role": "user", "content": "Hello"},
    {"role": "assistant", "content": "Hi!"},
    {"role": "user", "content": "What's the weather today?"}
  ]
}
```

与 OpenAI 的关键区别：`system` 是顶级字段，不在 `messages` 数组内。

非流式 JSON 响应：
```json
{
  "id": "msg_xxx",
  "type": "message",
  "role": "assistant",
  "content": [
    {"type": "text", "text": "Let me search for that."},
    {"type": "tool_use", "id": "toolu_xxx", "name": "web_search", "input": {"query": "weather today"}}
  ],
  "stop_reason": "tool_use"
}
```

当 `stop_reason` 为 `"tool_use"` 时，智能体循环执行各工具并将结果返回：
```json
{"role": "assistant", "content": [<text + tool_use blocks>]}
{"role": "user", "content": [{"type": "tool_result", "tool_use_id": "toolu_xxx", "content": "..."}]}
```

循环持续直到 `stop_reason` 为 `"end_turn"`（最多 10 次迭代）。

---

## 工具注册表

启动时在 `tools/tool_registry.c` 中注册 9 个内置工具：

| 工具               | 源文件               | 描述                                                       |
|--------------------|----------------------|------------------------------------------------------------|
| `web_search`       | `tool_web_search.c`  | Brave Search API（直连或经代理）                           |
| `get_current_time` | `tool_get_time.c`    | NTP 同步的当前日期 + 时间，并设置系统时钟                  |
| `read_file`        | `tool_files.c`       | 从 SPIFFS 读取文件（`/spiffs/...`）                        |
| `write_file`       | `tool_files.c`       | 向 SPIFFS 写入或覆盖文件                                   |
| `edit_file`        | `tool_files.c`       | 对 SPIFFS 文件执行首次匹配的查找替换                       |
| `list_dir`         | `tool_files.c`       | 列出 SPIFFS 文件（可按路径前缀过滤）                       |
| `cron_add`         | `tool_cron.c`        | 调度周期性（`every`）或一次性（`at`）任务                  |
| `cron_list`        | `tool_cron.c`        | 列出所有已调度的 Cron 任务                                 |
| `cron_remove`      | `tool_cron.c`        | 按 ID 删除 Cron 任务                                       |

每个工具定义为一个 `mimi_tool_t` 结构体，包含名称、描述、输入 JSON Schema 以及 `execute` 函数指针。每次 ReAct 迭代最多并行执行 4 个工具调用。

---

## 启动序列

```
app_main()
  ├── init_nvs()                    NVS flash init (erase if corrupted)
  ├── esp_event_loop_create_default()
  ├── init_spiffs()                 Mount SPIFFS at /spiffs
  ├── message_bus_init()            Create inbound + outbound queues
  ├── memory_store_init()           Verify SPIFFS paths
  ├── skill_loader_init()           Load built-in skills to SPIFFS, scan /spiffs/skills/
  ├── session_mgr_init()
  ├── wifi_manager_init()           Init WiFi STA mode + event handlers
  ├── http_proxy_init()             Load proxy config (NVS → build-time fallback)
  ├── telegram_bot_init()           Load bot token (NVS → build-time fallback)
  ├── llm_proxy_init()              Load API key + model (NVS → build-time fallback)
  ├── tool_registry_init()          Register 9 built-in tools, build tools JSON
  ├── cron_service_init()           Load persisted cron jobs from /spiffs/cron.json
  ├── heartbeat_init()              Create FreeRTOS timer (30 min interval)
  ├── agent_loop_init()
  ├── serial_cli_init()             Start REPL (works without WiFi)
  │
  ├── wifi_manager_start()          Connect using NVS credentials (fallback to build-time)
  │   └── wifi_manager_wait_connected(30s)
  │
  └── [if WiFi connected]
      ├── outbound_dispatch task    Launch outbound task (Core 0) — first to avoid drops
      ├── agent_loop_start()        Launch agent_loop task (Core 1)
      ├── telegram_bot_start()      Launch tg_poll task (Core 0)
      ├── cron_service_start()      Launch cron task (any core, prio 4)
      ├── heartbeat_start()         Start 30-min FreeRTOS timer
      └── ws_server_start()         Start httpd on port 18789
```

若 WiFi 凭据缺失或连接超时，CLI 仍可用于诊断。

---

## 串口 CLI 命令

CLI 提供运行时配置和调试/维护命令。通过 CLI 设置的配置保存至 NVS，重启后保留，并覆盖编译期值，直到执行 `config_reset` 为止。

**配置命令**（保存至 NVS）：

| 命令                                 | 描述                                                  |
|--------------------------------------|-------------------------------------------------------|
| `set_wifi <ssid> <password>`         | 设置 WiFi SSID 和密码                                 |
| `set_tg_token <token>`              | 设置 Telegram Bot 令牌                                |
| `set_api_key <key>`                 | 设置 LLM API 密钥（Anthropic 或 OpenAI）              |
| `set_model <model>`                 | 设置 LLM 模型标识符                                   |
| `set_model_provider <provider>`     | 设置提供商：`anthropic` 或 `openai`                   |
| `set_proxy <host> <port> [type]`    | 设置 HTTP/SOCKS5 代理（类型：`http` 或 `socks5`）     |
| `clear_proxy`                       | 清除代理配置                                          |
| `set_search_key <key>`              | 设置 Brave Search API 密钥                            |
| `config_show`                       | 显示当前配置（来源：NVS 或编译期）                    |
| `config_reset`                      | 清除所有 NVS 覆盖，恢复编译期默认值                   |

**调试与维护命令**：

| 命令                           | 描述                                             |
|--------------------------------|--------------------------------------------------|
| `wifi_status`                  | 显示连接状态和 IP 地址                           |
| `wifi_scan`                    | 扫描并列出附近的 WiFi 接入点                     |
| `memory_read`                  | 打印 MEMORY.md 内容                              |
| `memory_write <content>`       | 覆写 MEMORY.md                                   |
| `session_list`                 | 列出所有会话文件                                 |
| `session_clear <chat_id>`      | 删除指定会话文件                                 |
| `heap_info`                    | 显示内部 SRAM + PSRAM 可用字节数                 |
| `skill_list`                   | 列出已安装的技能                                 |
| `skill_show <name>`            | 打印指定技能文件的完整内容                       |
| `skill_search <keyword>`       | 按关键词搜索技能文件                             |
| `heartbeat_trigger`            | 手动触发一次心跳检查                             |
| `cron_start`                   | 启动 Cron 调度器任务                             |
| `tool_exec <name> [json]`      | 直接执行指定的已注册工具                         |
| `restart`                      | 重启设备                                         |
| `help`                         | 列出所有可用命令                                 |

---

## Nanobot 参考映射

| Nanobot 模块                    | MimiClaw 对应模块                        | 备注                                                       |
|---------------------------------|------------------------------------------|------------------------------------------------------------|
| `agent/loop.py`                 | `agent/agent_loop.c`                     | 带工具调用的 ReAct 循环                                    |
| `agent/context.py`              | `agent/context_builder.c`                | 加载 SOUL.md + USER.md + 记忆 + 技能摘要 + 工具引导        |
| `agent/memory.py`               | `memory/memory_store.c`                  | MEMORY.md + 每日日记                                       |
| `session/manager.py`            | `memory/session_mgr.c`                   | 每个会话一个 JSONL 文件，环形缓冲区                        |
| `channels/telegram.py`          | `telegram/telegram_bot.c`                | 原始 HTTP，不依赖 python-telegram-bot                      |
| `bus/events.py` + `queue.py`    | `bus/message_bus.c`                      | FreeRTOS 队列 vs asyncio                                   |
| `providers/litellm_provider.py` | `llm/llm_proxy.c`                        | 兼容 Anthropic + OpenAI 的 API                             |
| `config/schema.py`              | `mimi_config.h` + `mimi_secrets.h`       | 编译期 + NVS 运行时覆盖                                    |
| `cli/commands.py`               | `cli/serial_cli.c`                       | esp_console REPL，完整配置 + 调试命令                      |
| `agent/tools/*`                 | `tools/tool_registry.c` + `tool_*.c`     | 9 个内置工具（web_search、时间、文件 I/O、cron）           |
| `agent/subagent.py`             | *（尚未实现）*                           | 参见 TODO.md                                               |
| `agent/skills.py`               | `skills/skill_loader.c`                  | 从 /spiffs/skills/ 加载 .md 文件，注入系统提示词           |
| `cron/service.py`               | `cron/cron_service.c`                    | FreeRTOS 任务，支持 every/at 任务，持久化至 cron.json      |
| `heartbeat/service.py`          | `heartbeat/heartbeat.c`                  | FreeRTOS 定时器，30 分钟间隔，读取 HEARTBEAT.md            |
