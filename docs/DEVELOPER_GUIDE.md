# MimiClaw Developer Guide

> How to extend MimiClaw: adding tools, CLI commands, input channels, and embedded C best practices.

---

## Table of Contents

1. [Adding a New Tool](#1-adding-a-new-tool)
2. [Adding a New CLI Command](#2-adding-a-new-cli-command)
3. [Adding a New Input Channel](#3-adding-a-new-input-channel)
4. [Memory Management Rules](#4-memory-management-rules)
5. [SPIFFS File Operations](#5-spiffs-file-operations)
6. [Debugging Tips](#6-debugging-tips)

---

## 1. Adding a New Tool

Tools are the agent's actuators. Each tool is a `mimi_tool_t` struct registered in `tools/tool_registry.c`.

### Tool struct

Defined in `tools/tool_registry.h`:

```c
typedef struct {
    const char *name;               // Tool name sent to LLM (e.g. "web_search")
    const char *description;        // Natural language description for the LLM
    const char *input_schema_json;  // JSON Schema string for input validation
    esp_err_t (*execute)(const char *input_json, char *output, size_t output_size);
} mimi_tool_t;
```

### Step-by-step: add a `get_ip_info` tool

**1. Create the implementation file** `main/tools/tool_ip_info.h` / `tool_ip_info.c`:

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
    /* input_json ignored — no parameters needed */
    const char *ip = wifi_manager_get_ip();
    snprintf(output, output_size, "Device IP: %s", ip[0] ? ip : "not connected");
    return ESP_OK;
}
```

**2. Register in `tools/tool_registry.c`** — add after the last `register_tool()` call:

```c
#include "tools/tool_ip_info.h"

/* inside tool_registry_init(), before build_tools_json() */
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

**3. Add to `main/CMakeLists.txt`** — append `tools/tool_ip_info.c` to the `SRCS` list.

**4. Rebuild**: `idf.py build`

### JSON Schema rules

- Always use `"type":"object"` as the root
- List all required parameters in `"required":[...]`
- Use `"type":"string"`, `"type":"integer"`, `"type":"boolean"` for primitives
- Keep descriptions short and imperative — the LLM reads them to decide when to use the tool
- Parse `input_json` with `cJSON_Parse()` in your execute function; always check for NULL

### Output buffer conventions

- `output` is a caller-supplied buffer of `output_size` bytes
- Write a human-readable UTF-8 string; the LLM receives it as the tool result
- On error, write an error message and return `ESP_FAIL` — the agent loop will report it to the LLM
- Never write binary data; never exceed `output_size` (use `snprintf`)

---

## 2. Adding a New CLI Command

CLI commands use ESP-IDF's `esp_console` with `argtable3` for argument parsing.

### Pattern: no-argument command

```c
/* In serial_cli.c, add the handler function */
static int cmd_my_status(int argc, char **argv)
{
    printf("My module status: OK\n");
    return 0;
}

/* In serial_cli_init(), register the command */
esp_console_cmd_t my_cmd = {
    .command = "my_status",
    .help    = "Show my module status",
    .func    = &cmd_my_status,
};
esp_console_cmd_register(&my_cmd);
```

### Pattern: command with arguments (argtable3)

```c
/* Declare arg table at file scope */
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

/* In serial_cli_init() */
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

### Saving config to NVS

Follow the pattern used by `cmd_wifi_set` — open the NVS namespace, write the key, close:

```c
#include "nvs_flash.h"
#include "nvs.h"
#include "mimi_config.h"  /* NVS namespace + key defines */

nvs_handle_t nvs;
if (nvs_open(MIMI_NVS_LLM, NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_str(nvs, MIMI_NVS_KEY_MODEL, new_model);
    nvs_commit(nvs);
    nvs_close(nvs);
}
```

Reading NVS (with build-time fallback):

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

## 3. Adding a New Input Channel

A channel is any source of inbound messages (Telegram, WebSocket, serial, SMS, etc.).

### Required pattern

Every channel must implement two functions and follow the message bus contract:

```c
/* my_channel.h */
#pragma once
#include "esp_err.h"

esp_err_t my_channel_init(void);   /* load config, allocate resources */
esp_err_t my_channel_start(void);  /* launch FreeRTOS task on Core 0  */
```

### Pushing to the inbound queue

```c
#include "bus/message_bus.h"

/* Inside your channel's receive loop */
mimi_msg_t msg;
memset(&msg, 0, sizeof(msg));
strncpy(msg.channel, "mychannel", sizeof(msg.channel) - 1);
strncpy(msg.chat_id, sender_id,   sizeof(msg.chat_id)  - 1);
msg.content = strdup(text);  /* heap-allocated; ownership transferred */

if (msg.content) {
    esp_err_t err = message_bus_push_inbound(&msg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Inbound queue full, dropping message");
        free(msg.content);  /* free if push failed */
    }
}
```

### Adding an outbound route

In `mimi.c`, the `outbound_dispatch_task` routes by `msg.channel`. Add a branch:

```c
} else if (strcmp(msg.channel, "mychannel") == 0) {
    my_channel_send(msg.chat_id, msg.content);
}
```

### Wiring up in `app_main()`

```c
/* In mimi.c, app_main() */
ESP_ERROR_CHECK(my_channel_init());   /* add to init phase */
/* ... */
ESP_ERROR_CHECK(my_channel_start()); /* add to WiFi-connected phase */
```

Add the source file to `main/CMakeLists.txt`.

### Channel naming conventions

| Channel string  | Purpose                                 |
|-----------------|-----------------------------------------|
| `"telegram"`    | Telegram bot messages                   |
| `"websocket"`   | WebSocket LAN clients                   |
| `"system"`      | Internal agent triggers (cron, heartbeat)|
| `"cli"`         | Serial CLI input (not yet used for agent)|

---

## 4. Memory Management Rules

ESP32-S3 has ~512 KB internal SRAM and 8 MB PSRAM. The key rule: **large buffers go to PSRAM**.

### When to use PSRAM

```c
/* Anything >= ~4 KB: use PSRAM */
char *buf = heap_caps_calloc(1, 32 * 1024, MALLOC_CAP_SPIRAM);
if (!buf) {
    ESP_LOGE(TAG, "PSRAM alloc failed");
    return ESP_ERR_NO_MEM;
}
/* ... use buf ... */
free(buf);  /* same free() for both PSRAM and internal */
```

Use PSRAM for:
- HTTP response buffers (`MIMI_LLM_STREAM_BUF_SIZE` = 32 KB)
- cJSON parse trees from API responses
- System prompt / context buffers (`MIMI_CONTEXT_BUF_SIZE` = 16 KB)
- Session history arrays

### When to use stack / internal heap

- Small fixed-size structs (`mimi_msg_t`, `cron_job_t`)
- Short string buffers < 1 KB
- Argtable3 structures in CLI commands

### Hot-path rule

Never call `malloc`/`free` in a tight loop inside the agent ReAct iteration. Pre-allocate buffers once before the loop and reuse them:

```c
/* Good: allocate once, reuse across iterations */
char *tool_output = heap_caps_calloc(1, 4096, MALLOC_CAP_SPIRAM);
for (int iter = 0; iter < MIMI_AGENT_MAX_TOOL_ITER; iter++) {
    memset(tool_output, 0, 4096);
    tool_registry_execute(name, input, tool_output, 4096);
    /* ... */
}
free(tool_output);
```

### Stack sizes

FreeRTOS task stacks are in internal SRAM. Current allocations (from `mimi_config.h`):

| Task        | Stack  | Notes                                      |
|-------------|--------|--------------------------------------------|
| agent_loop  | 24 KB  | Large due to cJSON + context building      |
| outbound    | 12 KB  | Telegram HTTP send                         |
| tg_poll     | 12 KB  | Telegram long polling HTTP                 |
| cron        | 4 KB   | Simple timer loop                          |
| serial_cli  | 4 KB   | argtable3 arg parsing                      |

If you see `Task stack overflow` in logs, increase the relevant `*_STACK` constant in `mimi_config.h`.

---

## 5. SPIFFS File Operations

SPIFFS is a flat filesystem. "Directories" are just filename prefixes — there are no real subdirectories.

### Path conventions

| Prefix                     | Purpose                              |
|----------------------------|--------------------------------------|
| `/spiffs/config/`          | Bootstrap files (SOUL.md, USER.md)   |
| `/spiffs/memory/`          | MEMORY.md + daily notes              |
| `/spiffs/sessions/`        | Per-chat JSONL history               |
| `/spiffs/skills/`          | Skill .md files                      |
| `/spiffs/cron.json`        | Cron job store                       |
| `/spiffs/HEARTBEAT.md`     | Pending task list for heartbeat      |

### Standard read pattern

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

### Standard write pattern

```c
FILE *f = fopen("/spiffs/config/SOUL.md", "w");
if (!f) {
    ESP_LOGE(TAG, "Cannot open for write");
    return ESP_FAIL;
}
fputs(content, f);
fclose(f);
```

### Path length limit

SPIFFS stores full paths as filenames. Keep paths under **32 characters** total (including `/spiffs/` prefix) to avoid truncation. The partition is configured with `max_files = 10` open simultaneously.

### Checking available space

```c
size_t total = 0, used = 0;
esp_spiffs_info(NULL, &total, &used);
ESP_LOGI(TAG, "SPIFFS: %d/%d bytes used", (int)used, (int)total);
```

---

## 6. Debugging Tips

### Monitor heap health

Run `heap_info` in the serial CLI to see internal and PSRAM free bytes:

```
mimi> heap_info
Internal free: 142320 bytes
PSRAM free:    7823104 bytes
Total free:    7965424 bytes
```

Watch for internal heap dropping below ~50 KB — that indicates a stack overflow or a leak in a FreeRTOS task.

### Key ESP_LOGI log tags

| Tag         | Module                              |
|-------------|-------------------------------------|
| `mimi`      | app_main, outbound dispatch         |
| `agent`     | ReAct loop, tool execution          |
| `llm`       | HTTPS request/response              |
| `tools`     | Tool registration, dispatch         |
| `cron`      | Job scheduling, firing              |
| `heartbeat` | Timer, HEARTBEAT.md check           |
| `skills`    | Skill file loading                  |
| `tg`        | Telegram polling + send             |
| `ws`        | WebSocket server                    |
| `wifi`      | WiFi events, reconnect              |
| `cli`       | Serial REPL                         |

### Checking SPIFFS contents

```
mimi> tool_exec list_dir '{"prefix":"/spiffs/"}'
```

This runs the `list_dir` tool directly and prints all files.

### Testing a tool without the agent

```
mimi> tool_exec web_search '{"query":"ESP32 FreeRTOS heap"}'
mimi> tool_exec get_current_time '{}'
mimi> tool_exec read_file '{"path":"/spiffs/memory/MEMORY.md"}'
```

### Triggering heartbeat / cron manually

```
mimi> heartbeat_trigger
mimi> cron_start
```

### Verbose LLM payload logging

Set `MIMI_LLM_LOG_VERBOSE_PAYLOAD 1` in `mimi_config.h` and rebuild to log the first `MIMI_LLM_LOG_PREVIEW_BYTES` bytes of each API request/response. Useful for diagnosing JSON serialization issues.

### Watching the agent loop

The agent logs each iteration:
```
I (12345) agent: [iter 1/10] Calling LLM...
I (13456) agent: stop_reason=tool_use, executing 2 tool(s)
I (13457) tools: Executing tool: web_search
I (14500) agent: [iter 2/10] Calling LLM...
I (15600) agent: stop_reason=end_turn, done
```

If the loop hits iteration 10 without `end_turn`, the agent is stuck in a tool loop — check the tool output for errors.
