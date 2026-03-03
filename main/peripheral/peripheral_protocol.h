#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* Maximum tools a peripheral can declare */
#define PDP_MAX_TOOLS   23
/* Maximum skills a peripheral can transfer */
#define PDP_MAX_SKILLS  8
/* Max length for a tool name */
#define PDP_TOOL_NAME_LEN   48
/* Max length for a string field */
#define PDP_STR_LEN     128
/* Max length for JSON schema stored per-tool */
#define PDP_SCHEMA_LEN  1024
/* Max length for a skill file name */
#define PDP_SKILL_NAME_LEN  64

/** Single tool entry from the peripheral manifest */
typedef struct {
    char name[PDP_TOOL_NAME_LEN];
    char description[PDP_STR_LEN];
    char input_schema_json[PDP_SCHEMA_LEN];
} pdp_tool_t;

/** Parsed representation of a peripheral manifest */
typedef struct {
    char device_name[PDP_TOOL_NAME_LEN];        /* e.g. "robotic_arm"        */
    char display_name[PDP_STR_LEN];             /* e.g. "6-DOF Robotic Arm"  */
    char version[32];                           /* e.g. "1.0.0"              */
    char description[PDP_STR_LEN];
    int  tool_count;
    pdp_tool_t tools[PDP_MAX_TOOLS];
    int  skill_count;
    char skill_names[PDP_MAX_SKILLS][PDP_SKILL_NAME_LEN];
} pdp_manifest_t;

/**
 * Perform the PDP v1 handshake with an attached peripheral.
 * Sends hello, receives ack, requests manifest, transfers skill files,
 * confirms ready.
 *
 * Skill file contents are written directly to SPIFFS at
 * MIMI_PERIPH_SKILLS_PREFIX<name>.md during the handshake.
 *
 * @param manifest  Output: parsed manifest (caller provides storage)
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if peripheral does not respond,
 *         ESP_FAIL on protocol error
 */
esp_err_t pdp_handshake(pdp_manifest_t *manifest);

/**
 * Send a tool_call to the peripheral and wait for the result.
 *
 * @param tool_name  Tool name string
 * @param call_id    Unique call ID (echoed back in result)
 * @param input_json JSON input for the tool
 * @param output     Output buffer for the tool result text
 * @param output_sz  Output buffer size
 * @return ESP_OK if tool_result ok==true,
 *         ESP_FAIL if ok==false or timeout,
 *         ESP_ERR_TIMEOUT if no response within MIMI_PERIPH_TOOL_TIMEOUT_MS
 */
esp_err_t pdp_tool_call(const char *tool_name, const char *call_id,
                         const char *input_json,
                         char *output, size_t output_sz);
