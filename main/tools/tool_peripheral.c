#include "tool_peripheral.h"
#include "tool_registry.h"
#include "peripheral/peripheral_protocol.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "tool_periph";

/* Simple call ID generator: hex counter */
static uint32_t s_call_counter = 0;

esp_err_t tool_peripheral_execute(const char *input_json,
                                  char *output, size_t output_size)
{
    /* Retrieve the tool name set by tool_registry_execute() */
    const char *tool_name = tool_registry_get_current_tool_name();
    if (!tool_name) {
        snprintf(output, output_size, "Error: no current tool name (internal error)");
        return ESP_FAIL;
    }

    /* Generate a unique call ID */
    char call_id[16];
    snprintf(call_id, sizeof(call_id), "%08lx", (unsigned long)++s_call_counter);

    ESP_LOGI(TAG, "Forwarding tool %s (id=%s) to peripheral", tool_name, call_id);

    return pdp_tool_call(tool_name, call_id, input_json ? input_json : "{}",
                         output, output_size);
}
