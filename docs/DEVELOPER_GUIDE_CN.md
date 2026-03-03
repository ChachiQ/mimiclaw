# MimiClaw 开发者指南

> 如何扩展 MimiClaw：添加工具、CLI 命令、输入通道，以及嵌入式 C 最佳实践。

---

## 目录

1. [添加新工具](#1-添加新工具)
2. [添加新 CLI 命令](#2-添加新-cli-命令)
3. [添加新输入通道](#3-添加新输入通道)
4. [内存管理规范](#4-内存管理规范)
5. [SPIFFS 文件操作](#5-spiffs-文件操作)
6. [调试技巧](#6-调试技巧)

---

## 1. 添加新工具

工具是 Agent 的执行器。每个工具都是一个 `mimi_tool_t` 结构体，在 `tools/tool_registry.c` 中注册。

### 工具结构体

定义于 `tools/tool_registry.h`：

```c
typedef struct {
    const char *name;               // 发送给 LLM 的工具名称（例如 "web_search"）
    const char *description;        // 供 LLM 理解的自然语言描述
    const char *input_schema_json;  // 用于输入验证的 JSON Schema 字符串
    esp_err_t (*execute)(const char *input_json, char *output, size_t output_size);
} mimi_tool_t;
```

### 逐步示例：添加 `get_ip_info` 工具

**1. 创建实现文件** `main/tools/tool_ip_info.h` / `tool_ip_info.c`：

```c
/* tool_ip_info.h */
#pragma once
#include "esp_err.h"
#include <stddef.h>

esp_err_t tool_get_ip_info_execute(const char *input_json,
                                   char *output, size_t output_size);
```

```c
/* tool_ip_info.c */
#include "tool_ip_info.h"
#include "wifi/wifi_manager.h"
#include <stdio.h>

esp_err_t tool_get_ip_info_execute(const char *input_json,
                                   char *output, size_t output_size)
{
    /* 忽略 input_json — 此工具无需任何参数 */
    const char *ip = wifi_manager_get_ip();
    snprintf(output, output_size, "Device IP: %s", ip[0] ? ip : "not connected");
    return ESP_OK;
}
```

**2. 在 `tools/tool_registry.c` 中注册** — 在最后一个 `register_tool()` 调用之后追加：

```c
#include "tools/tool_ip_info.h"

/* 在 tool_registry_init() 内部，build_tools_json() 调用之前 */
mimi_tool_t ip_tool = {
    .name = "get_ip_info",
    .description = "Get the device's current IP address and network status.",
    .input_schema_json =
        "{\"type\":\"object\","
        "\"properties\":{},"
        "\"required\":[]}",
    .execute = tool_get_ip_info_execute,
};
register_tool(&ip_tool);
```

**3. 在 `main/CMakeLists.txt` 中添加** — 将 `tools/tool_ip_info.c` 追加到 `SRCS` 列表。

**4. 重新编译**：`idf.py build`

### JSON Schema 规范

- 根类型始终使用 `"type":"object"`
- 在 `"required":[...]` 中列出所有必填参数
- 基本类型使用 `"type":"string"`、`"type":"integer"`、`"type":"boolean"`
- 描述应简短且使用祈使语气 — LLM 通过描述判断何时调用该工具
- 在 execute 函数中使用 `cJSON_Parse()` 解析 `input_json`；务必检查返回值是否为 NULL

### 输出缓冲区约定

- `output` 是调用方提供的、大小为 `output_size` 字节的缓冲区
- 写入人类可读的 UTF-8 字符串；LLM 将把它作为工具调用结果接收
- 出错时写入错误信息并返回 `ESP_FAIL` — Agent 循环会将其上报给 LLM
- 禁止写入二进制数据；禁止超出 `output_size`（使用 `snprintf`）

---

## 2. 添加新 CLI 命令

CLI 命令使用 ESP-IDF 的 `esp_console`，并结合 `argtable3` 进行参数解析。

### 模式：无参数命令

```c
/* 在 serial_cli.c 中添加处理函数 */
static int cmd_my_status(int argc, char **argv)
{
    printf("My module status: OK\n");
    return 0;
}

/* 在 serial_cli_init() 中注册命令 */
esp_console_cmd_t my_cmd = {
    .command = "my_status",
    .help    = "Show my module status",
    .func    = &cmd_my_status,
};
esp_console_cmd_register(&my_cmd);
```

### 模式：带参数的命令（argtable3）

```c
/* 在文件作用域声明参数表 */
static struct {
    struct arg_str *name;
    struct arg_int *count;
    struct arg_end *end;
} my_args;

static int cmd_my_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&my_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, my_args.end, argv[0]);
        return 1;
    }
    const char *name  = my_args.name->sval[0];
    int         count = my_args.count->ival[0];
    printf("Set %s = %d\n", name, count);
    return 0;
}

/* 在 serial_cli_init() 中 */
my_args.name  = arg_str1(NULL, NULL, "<name>",  "Item name");
my_args.count = arg_int1(NULL, NULL, "<count>", "Item count");
my_args.end   = arg_end(2);
esp_console_cmd_t my_set_cmd = {
    .command  = "my_set",
    .help     = "Set item name and count",
    .func     = &cmd_my_set,
    .argtable = &my_args,
};
esp_console_cmd_register(&my_set_cmd);
```

### 将配置保存到 NVS

参照 `cmd_wifi_set` 的写法 — 打开 NVS 命名空间，写入键值，关闭：

```c
#include "nvs_flash.h"
#include "nvs.h"
#include "mimi_config.h"  /* NVS 命名空间 + 键名宏定义 */

nvs_handle_t nvs;
if (nvs_open(MIMI_NVS_LLM, NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_str(nvs, MIMI_NVS_KEY_MODEL, new_model);
    nvs_commit(nvs);
    nvs_close(nvs);
}
```

从 NVS 读取（附编译期回退默认值）：

```c
char value[64] = {0};
nvs_handle_t nvs;
if (nvs_open(MIMI_NVS_LLM, NVS_READONLY, &nvs) == ESP_OK) {
    size_t len = sizeof(value);
    nvs_get_str(nvs, MIMI_NVS_KEY_MODEL, value, &len);
    nvs_close(nvs);
}
if (!value[0]) {
    strncpy(value, MIMI_SECRET_MODEL, sizeof(value) - 1);
}
```

---

## 3. 添加新输入通道

通道是任何入站消息的来源（Telegram、WebSocket、串口、短信等）。

### 必须遵循的模式

每个通道必须实现两个函数，并遵守消息总线契约：

```c
/* my_channel.h */
#pragma once
#include "esp_err.h"

esp_err_t my_channel_init(void);   /* 加载配置，分配资源 */
esp_err_t my_channel_start(void);  /* 在 Core 0 上启动 FreeRTOS 任务 */
```

### 向入站队列推送消息

```c
#include "bus/message_bus.h"

/* 在通道的接收循环内部 */
mimi_msg_t msg;
memset(&msg, 0, sizeof(msg));
strncpy(msg.channel, "mychannel", sizeof(msg.channel) - 1);
strncpy(msg.chat_id, sender_id,   sizeof(msg.chat_id)  - 1);
msg.content = strdup(text);  /* 堆分配；所有权转移给队列 */

if (msg.content) {
    esp_err_t err = message_bus_push_inbound(&msg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Inbound queue full, dropping message");
        free(msg.content);  /* 推送失败时释放内存 */
    }
}
```

### 添加出站路由

在 `mimi.c` 中，`outbound_dispatch_task` 按 `msg.channel` 进行路由分发。添加一个分支：

```c
} else if (strcmp(msg.channel, "mychannel") == 0) {
    my_channel_send(msg.chat_id, msg.content);
}
```

### 在 `app_main()` 中接入

```c
/* 在 mimi.c 的 app_main() 中 */
ESP_ERROR_CHECK(my_channel_init());   /* 添加到初始化阶段 */
/* ... */
ESP_ERROR_CHECK(my_channel_start()); /* 添加到 WiFi 连接成功后的阶段 */
```

将源文件添加到 `main/CMakeLists.txt`。

### 通道命名约定

| 通道字符串      | 用途                                    |
|-----------------|-----------------------------------------|
| `"telegram"`    | Telegram 机器人消息                     |
| `"websocket"`   | WebSocket 局域网客户端                  |
| `"system"`      | Agent 内部触发（定时任务、心跳）        |
| `"cli"`         | 串口 CLI 输入（暂未用于 Agent）         |

---

## 4. 内存管理规范

ESP32-S3 拥有约 512 KB 内部 SRAM 和 8 MB PSRAM。核心原则：**大缓冲区放 PSRAM**。

### 何时使用 PSRAM

```c
/* 大小 >= 约 4 KB 时：使用 PSRAM */
char *buf = heap_caps_calloc(1, 32 * 1024, MALLOC_CAP_SPIRAM);
if (!buf) {
    ESP_LOGE(TAG, "PSRAM alloc failed");
    return ESP_ERR_NO_MEM;
}
/* ... 使用 buf ... */
free(buf);  /* PSRAM 和内部堆均使用同一个 free() */
```

以下情况应使用 PSRAM：
- HTTP 响应缓冲区（`MIMI_LLM_STREAM_BUF_SIZE` = 32 KB）
- 来自 API 响应的 cJSON 解析树
- 系统提示词 / 上下文缓冲区（`MIMI_CONTEXT_BUF_SIZE` = 16 KB）
- 会话历史数组

### 何时使用栈 / 内部堆

- 小型固定大小结构体（`mimi_msg_t`、`cron_job_t`）
- 小于 1 KB 的短字符串缓冲区
- CLI 命令中的 argtable3 结构体

### 热路径规范

禁止在 Agent ReAct 迭代的紧密循环中调用 `malloc`/`free`。应在循环前一次性预分配缓冲区并复用：

```c
/* 正确做法：一次分配，跨迭代复用 */
char *tool_output = heap_caps_calloc(1, 4096, MALLOC_CAP_SPIRAM);
for (int iter = 0; iter < MIMI_AGENT_MAX_TOOL_ITER; iter++) {
    memset(tool_output, 0, 4096);
    tool_registry_execute(name, input, tool_output, 4096);
    /* ... */
}
free(tool_output);
```

### 栈大小

FreeRTOS 任务栈位于内部 SRAM。当前各任务分配（来自 `mimi_config.h`）：

| 任务        | 栈大小 | 说明                                          |
|-------------|--------|-----------------------------------------------|
| agent_loop  | 24 KB  | 因 cJSON 解析和上下文构建需求较大             |
| outbound    | 12 KB  | Telegram HTTP 发送                            |
| tg_poll     | 12 KB  | Telegram 长轮询 HTTP                          |
| cron        | 4 KB   | 简单定时循环                                  |
| serial_cli  | 4 KB   | argtable3 参数解析                            |

若日志中出现 `Task stack overflow`，请在 `mimi_config.h` 中增大对应的 `*_STACK` 常量。

---

## 5. SPIFFS 文件操作

SPIFFS 是平坦文件系统。"目录"仅是文件名前缀 — 不存在真正的子目录层级。

### 路径约定

| 前缀                       | 用途                                 |
|----------------------------|--------------------------------------|
| `/spiffs/config/`          | 引导配置文件（SOUL.md、USER.md）     |
| `/spiffs/memory/`          | MEMORY.md 及每日笔记                 |
| `/spiffs/sessions/`        | 每个会话的 JSONL 历史记录            |
| `/spiffs/skills/`          | 技能 .md 文件                        |
| `/spiffs/cron.json`        | 定时任务存储                         |
| `/spiffs/HEARTBEAT.md`     | 心跳待处理任务列表                   |

### 标准读取模式

```c
FILE *f = fopen("/spiffs/memory/MEMORY.md", "r");
if (!f) {
    ESP_LOGW(TAG, "File not found");
    return ESP_ERR_NOT_FOUND;
}
char buf[4096];
size_t n = fread(buf, 1, sizeof(buf) - 1, f);
buf[n] = '\0';
fclose(f);
```

### 标准写入模式

```c
FILE *f = fopen("/spiffs/config/SOUL.md", "w");
if (!f) {
    ESP_LOGE(TAG, "Cannot open for write");
    return ESP_FAIL;
}
fputs(content, f);
fclose(f);
```

### 路径长度限制

SPIFFS 将完整路径作为文件名存储。路径总长度（含 `/spiffs/` 前缀）请保持在 **32 个字符**以内，以避免截断。分区配置为最多同时打开 `max_files = 10` 个文件。

### 检查可用空间

```c
size_t total = 0, used = 0;
esp_spiffs_info(NULL, &total, &used);
ESP_LOGI(TAG, "SPIFFS: %d/%d bytes used", (int)used, (int)total);
```

---

## 6. 调试技巧

### 监控堆状态

在串口 CLI 中运行 `heap_info` 查看内部堆和 PSRAM 的剩余字节数：

```
mimi> heap_info
Internal free: 142320 bytes
PSRAM free:    7823104 bytes
Total free:    7965424 bytes
```

若内部堆剩余低于约 50 KB，说明可能存在 FreeRTOS 任务的栈溢出或内存泄漏。

### 关键 ESP_LOGI 日志标签

| 标签        | 模块                                |
|-------------|-------------------------------------|
| `mimi`      | app_main、出站分发                  |
| `agent`     | ReAct 循环、工具执行                |
| `llm`       | HTTPS 请求/响应                     |
| `tools`     | 工具注册、分发调用                  |
| `cron`      | 任务调度、触发执行                  |
| `heartbeat` | 定时器、HEARTBEAT.md 检查           |
| `skills`    | 技能文件加载                        |
| `tg`        | Telegram 轮询及发送                 |
| `ws`        | WebSocket 服务器                    |
| `wifi`      | WiFi 事件、重连                     |
| `cli`       | 串口 REPL                           |

### 查看 SPIFFS 内容

```
mimi> tool_exec list_dir '{"prefix":"/spiffs/"}'
```

该命令直接运行 `list_dir` 工具并打印所有文件。

### 不经过 Agent 直接测试工具

```
mimi> tool_exec web_search '{"query":"ESP32 FreeRTOS heap"}'
mimi> tool_exec get_current_time '{}'
mimi> tool_exec read_file '{"path":"/spiffs/memory/MEMORY.md"}'
```

### 手动触发心跳 / 定时任务

```
mimi> heartbeat_trigger
mimi> cron_start
```

### 开启详细 LLM 负载日志

在 `mimi_config.h` 中将 `MIMI_LLM_LOG_VERBOSE_PAYLOAD` 设置为 `1` 并重新编译，即可记录每次 API 请求/响应前 `MIMI_LLM_LOG_PREVIEW_BYTES` 字节的内容。适用于诊断 JSON 序列化问题。

### 观察 Agent 循环运行

Agent 会记录每次迭代的日志：
```
I (12345) agent: [iter 1/10] Calling LLM...
I (13456) agent: stop_reason=tool_use, executing 2 tool(s)
I (13457) tools: Executing tool: web_search
I (14500) agent: [iter 2/10] Calling LLM...
I (15600) agent: stop_reason=end_turn, done
```

若循环跑满 10 次迭代仍未出现 `end_turn`，说明 Agent 陷入了工具调用死循环 — 请检查工具输出是否存在错误。
