#include "peripheral_manager.h"
#include "peripheral_protocol.h"
#include "peripheral_uart.h"
#include "mimi_config.h"
#include "tools/tool_registry.h"
#include "tools/tool_peripheral.h"
#include "agent/context_builder.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "periph_mgr";

static bool s_connected = false;
static pdp_manifest_t s_manifest;

/* Storage for dynamically registered tool structs (heap-allocated on connect) */
static mimi_tool_t *s_dyn_tools = NULL;

esp_err_t peripheral_manager_init(void)
{
    memset(&s_manifest, 0, sizeof(s_manifest));
    s_connected = false;
    ESP_LOGI(TAG, "Peripheral manager initialized");
    return ESP_OK;
}

static esp_err_t save_manifest_to_spiffs(void)
{
    /* Ensure directory exists */
    char dir[64];
    snprintf(dir, sizeof(dir), "%s", MIMI_PERIPH_DIR);
    mkdir(dir, 0777);  /* SPIFFS mkdir is a no-op */

    FILE *f = fopen(MIMI_PERIPH_MANIFEST_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot write manifest file");
        return ESP_FAIL;
    }

    /* Write a readable JSON summary */
    fprintf(f, "{\"device\":{\"name\":\"%s\",\"display_name\":\"%s\","
               "\"version\":\"%s\",\"description\":\"%s\"},"
               "\"tool_count\":%d,\"skill_count\":%d}\n",
            s_manifest.device_name, s_manifest.display_name,
            s_manifest.version, s_manifest.description,
            s_manifest.tool_count, s_manifest.skill_count);
    fclose(f);
    ESP_LOGI(TAG, "Manifest saved to SPIFFS");
    return ESP_OK;
}

void peripheral_manager_on_connect(void)
{
    ESP_LOGI(TAG, "Peripheral connected — starting handshake");

    esp_err_t ret = pdp_handshake(&s_manifest);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PDP handshake failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Save manifest to SPIFFS */
    save_manifest_to_spiffs();

    /* Allocate tool struct array */
    free(s_dyn_tools);
    s_dyn_tools = calloc(s_manifest.tool_count, sizeof(mimi_tool_t));
    if (!s_dyn_tools && s_manifest.tool_count > 0) {
        ESP_LOGE(TAG, "Cannot allocate dynamic tool storage");
        return;
    }

    /* Register each peripheral tool */
    for (int i = 0; i < s_manifest.tool_count; i++) {
        pdp_tool_t *pt = &s_manifest.tools[i];
        mimi_tool_t *mt = &s_dyn_tools[i];

        mt->name             = pt->name;
        mt->description      = pt->description;
        mt->input_schema_json = pt->input_schema_json;
        mt->execute          = tool_peripheral_execute;
        mt->is_dynamic       = true;

        esp_err_t reg_ret = tool_registry_register_dynamic(mt);
        if (reg_ret != ESP_OK) {
            ESP_LOGW(TAG, "Could not register tool %s: %s",
                     pt->name, esp_err_to_name(reg_ret));
        }
    }

    /* Rebuild tools JSON after all tools are registered */
    tool_registry_rebuild_json();

    /* Build peripheral info string for context builder */
    char tool_list[256] = {0};
    for (int i = 0; i < s_manifest.tool_count; i++) {
        if (i > 0) strlcat(tool_list, ", ", sizeof(tool_list));
        strlcat(tool_list, s_manifest.tools[i].name, sizeof(tool_list));
    }

    char info[256];
    snprintf(info, sizeof(info),
             "## Connected Peripheral: %s (%s v%s)\n"
             "Description: %s\n"
             "Available tools: %s\n",
             s_manifest.display_name, s_manifest.device_name, s_manifest.version,
             s_manifest.description,
             tool_list);

    context_builder_set_peripheral_info(info);
    s_connected = true;

    ESP_LOGI(TAG, "Peripheral ready: %s (%d tools)", s_manifest.device_name, s_manifest.tool_count);
}

void peripheral_manager_on_disconnect(void)
{
    ESP_LOGI(TAG, "Peripheral disconnected");

    tool_registry_unregister_peripheral_tools();
    tool_registry_rebuild_json();
    context_builder_clear_peripheral_info();

    free(s_dyn_tools);
    s_dyn_tools = NULL;
    s_connected = false;

    ESP_LOGI(TAG, "Dynamic tools unregistered");
}

bool peripheral_manager_is_connected(void)
{
    return s_connected;
}

const char *peripheral_manager_get_name(void)
{
    return s_connected ? s_manifest.device_name : NULL;
}
