#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Universal execute function for all dynamically-registered peripheral tools.
 * Serializes the call as a PDP tool_call frame, sends it via UART1,
 * waits for the tool_result response, and writes the result to output.
 *
 * This function is assigned as the .execute pointer for every tool
 * registered from a peripheral manifest. The tool name is extracted
 * from the registry at dispatch time.
 *
 * @param input_json   JSON string of tool input arguments
 * @param output       Output buffer for result text
 * @param output_size  Output buffer size
 * @return ESP_OK if peripheral returned ok=true,
 *         ESP_FAIL if ok=false or timeout
 */
esp_err_t tool_peripheral_execute(const char *input_json,
                                  char *output, size_t output_size);
