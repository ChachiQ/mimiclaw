#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    const char *name;
    const char *description;
    const char *input_schema_json;  /* JSON Schema string for input */
    esp_err_t (*execute)(const char *input_json, char *output, size_t output_size);
    bool is_dynamic;                /* true = registered at runtime by peripheral */
} mimi_tool_t;

/**
 * Initialize tool registry and register all built-in tools.
 */
esp_err_t tool_registry_init(void);

/**
 * Get the pre-built tools JSON array string for the API request.
 * Returns NULL if no tools are registered.
 */
const char *tool_registry_get_tools_json(void);

/**
 * Execute a tool by name.
 *
 * @param name         Tool name (e.g. "web_search")
 * @param input_json   JSON string of tool input
 * @param output       Output buffer for tool result text
 * @param output_size  Size of output buffer
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if tool unknown
 */
esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size);

/**
 * Dynamically register a peripheral tool at runtime.
 * The tool struct must remain valid until unregistered.
 * Call tool_registry_rebuild_json() after registering all tools.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if registry full
 */
esp_err_t tool_registry_register_dynamic(const mimi_tool_t *tool);

/**
 * Unregister all dynamically-registered (peripheral) tools.
 * Call tool_registry_rebuild_json() afterwards to update the JSON.
 */
void tool_registry_unregister_peripheral_tools(void);

/**
 * Rebuild the cached tools JSON array.
 * Must be called after adding or removing dynamic tools.
 */
void tool_registry_rebuild_json(void);

/**
 * Returns the name of the tool currently being dispatched.
 * Valid only during an active tool_registry_execute() call.
 * Used by tool_peripheral_execute() to know which tool to invoke on the peripheral.
 */
const char *tool_registry_get_current_tool_name(void);
