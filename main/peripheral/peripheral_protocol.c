#include "peripheral_protocol.h"
#include "peripheral_uart.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "pdp";

/* Scratch buffer for one UART frame (4 KB, on stack — callers must have enough stack) */
#define FRAME_BUF_SIZE  4096

static char s_frame[FRAME_BUF_SIZE];

/* Helper: send a small JSON object */
static esp_err_t send_json(cJSON *obj)
{
    char *str = cJSON_PrintUnformatted(obj);
    if (!str) return ESP_ERR_NO_MEM;
    esp_err_t ret = peripheral_uart_send(str);
    free(str);
    return ret;
}

/* Helper: receive one frame and parse it as JSON */
static cJSON *recv_json(int timeout_ms)
{
    esp_err_t ret = peripheral_uart_recv(s_frame, sizeof(s_frame), timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "recv failed: %s", esp_err_to_name(ret));
        return NULL;
    }
    cJSON *obj = cJSON_Parse(s_frame);
    if (!obj) {
        ESP_LOGW(TAG, "JSON parse failed: %.64s", s_frame);
    }
    return obj;
}

/* Helper: check type field of a JSON object */
static bool json_type_is(cJSON *obj, const char *expected)
{
    cJSON *t = cJSON_GetObjectItem(obj, "type");
    return t && cJSON_IsString(t) && strcmp(t->valuestring, expected) == 0;
}

/* Write skill content to SPIFFS */
static esp_err_t write_skill(const char *name, const char *content)
{
    char path[128];
    snprintf(path, sizeof(path), "%s%s.md", MIMI_PERIPH_SKILLS_PREFIX, name);

    /* Ensure directory exists */
    {
        char dir[96];
        snprintf(dir, sizeof(dir), MIMI_SPIFFS_BASE "/skills");
        mkdir(dir, 0777);   /* SPIFFS mkdir is a no-op but doesn't fail */
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot write skill file: %s", path);
        return ESP_FAIL;
    }
    fputs(content, f);
    fclose(f);
    ESP_LOGI(TAG, "Skill written: %s (%d bytes)", path, (int)strlen(content));
    return ESP_OK;
}

esp_err_t pdp_handshake(pdp_manifest_t *manifest)
{
    if (!manifest) return ESP_ERR_INVALID_ARG;

    peripheral_uart_flush();

    /* Step 1: Send hello */
    {
        cJSON *hello = cJSON_CreateObject();
        cJSON_AddStringToObject(hello, "type", "hello");
        cJSON_AddStringToObject(hello, "ver", "1");
        send_json(hello);
        cJSON_Delete(hello);
        ESP_LOGI(TAG, "Sent hello");
    }

    /* Step 2: Wait for ack */
    {
        cJSON *ack = recv_json(MIMI_PERIPH_HANDSHAKE_TIMEOUT_MS);
        if (!ack) {
            ESP_LOGE(TAG, "No ack from peripheral");
            return ESP_ERR_TIMEOUT;
        }
        if (!json_type_is(ack, "ack")) {
            ESP_LOGE(TAG, "Expected ack, got: %.32s", s_frame);
            cJSON_Delete(ack);
            return ESP_FAIL;
        }
        cJSON *name_j = cJSON_GetObjectItem(ack, "name");
        if (name_j && cJSON_IsString(name_j)) {
            ESP_LOGI(TAG, "Peripheral ack: name=%s", name_j->valuestring);
        }
        cJSON_Delete(ack);
    }

    /* Step 3: Request manifest */
    {
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "type", "manifest_req");
        send_json(req);
        cJSON_Delete(req);
    }

    /* Step 4: Receive manifest */
    {
        cJSON *mf = recv_json(MIMI_PERIPH_HANDSHAKE_TIMEOUT_MS);
        if (!mf) {
            ESP_LOGE(TAG, "No manifest from peripheral");
            return ESP_ERR_TIMEOUT;
        }
        if (!json_type_is(mf, "manifest")) {
            ESP_LOGE(TAG, "Expected manifest, got: %.32s", s_frame);
            cJSON_Delete(mf);
            return ESP_FAIL;
        }

        /* Parse device fields */
        cJSON *dev = cJSON_GetObjectItem(mf, "device");
        if (dev) {
            cJSON *j;
            j = cJSON_GetObjectItem(dev, "name");
            if (j && cJSON_IsString(j))
                snprintf(manifest->device_name, sizeof(manifest->device_name), "%s", j->valuestring);
            j = cJSON_GetObjectItem(dev, "display_name");
            if (j && cJSON_IsString(j))
                snprintf(manifest->display_name, sizeof(manifest->display_name), "%s", j->valuestring);
            j = cJSON_GetObjectItem(dev, "version");
            if (j && cJSON_IsString(j))
                snprintf(manifest->version, sizeof(manifest->version), "%s", j->valuestring);
            j = cJSON_GetObjectItem(dev, "description");
            if (j && cJSON_IsString(j))
                snprintf(manifest->description, sizeof(manifest->description), "%s", j->valuestring);
        }

        /* Parse tools */
        manifest->tool_count = 0;
        cJSON *tools_arr = cJSON_GetObjectItem(mf, "tools");
        if (cJSON_IsArray(tools_arr)) {
            cJSON *t;
            cJSON_ArrayForEach(t, tools_arr) {
                if (manifest->tool_count >= PDP_MAX_TOOLS) break;
                pdp_tool_t *pt = &manifest->tools[manifest->tool_count];

                cJSON *j = cJSON_GetObjectItem(t, "name");
                if (j && cJSON_IsString(j))
                    snprintf(pt->name, sizeof(pt->name), "%s", j->valuestring);
                else
                    continue;  /* skip unnamed tools */

                j = cJSON_GetObjectItem(t, "description");
                if (j && cJSON_IsString(j))
                    snprintf(pt->description, sizeof(pt->description), "%s", j->valuestring);

                j = cJSON_GetObjectItem(t, "input_schema");
                if (j) {
                    char *schema_str = cJSON_PrintUnformatted(j);
                    if (schema_str) {
                        snprintf(pt->input_schema_json, sizeof(pt->input_schema_json), "%s", schema_str);
                        free(schema_str);
                    }
                } else {
                    snprintf(pt->input_schema_json, sizeof(pt->input_schema_json),
                             "{\"type\":\"object\",\"properties\":{},\"required\":[]}");
                }

                manifest->tool_count++;
            }
        }

        /* Parse skill names */
        manifest->skill_count = 0;
        cJSON *skills_arr = cJSON_GetObjectItem(mf, "skills");
        if (cJSON_IsArray(skills_arr)) {
            cJSON *s;
            cJSON_ArrayForEach(s, skills_arr) {
                if (manifest->skill_count >= PDP_MAX_SKILLS) break;
                if (cJSON_IsString(s)) {
                    snprintf(manifest->skill_names[manifest->skill_count],
                             PDP_SKILL_NAME_LEN, "%s", s->valuestring);
                    manifest->skill_count++;
                }
            }
        }

        cJSON_Delete(mf);
        ESP_LOGI(TAG, "Manifest parsed: device=%s, tools=%d, skills=%d",
                 manifest->device_name, manifest->tool_count, manifest->skill_count);
    }

    /* Step 5: Transfer skill files */
    for (int i = 0; i < manifest->skill_count; i++) {
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "type", "skill_req");
        cJSON_AddNumberToObject(req, "index", i);
        send_json(req);
        cJSON_Delete(req);

        cJSON *skill_msg = recv_json(MIMI_PERIPH_HANDSHAKE_TIMEOUT_MS);
        if (!skill_msg) {
            ESP_LOGW(TAG, "No skill response for index %d", i);
            continue;
        }
        if (!json_type_is(skill_msg, "skill")) {
            ESP_LOGW(TAG, "Expected skill, got: %.32s", s_frame);
            cJSON_Delete(skill_msg);
            continue;
        }

        cJSON *name_j = cJSON_GetObjectItem(skill_msg, "name");
        cJSON *content_j = cJSON_GetObjectItem(skill_msg, "content");
        if (name_j && cJSON_IsString(name_j) &&
            content_j && cJSON_IsString(content_j)) {
            write_skill(name_j->valuestring, content_j->valuestring);
        }
        cJSON_Delete(skill_msg);
    }

    /* Step 6: Send ready */
    {
        cJSON *rdy = cJSON_CreateObject();
        cJSON_AddStringToObject(rdy, "type", "ready");
        send_json(rdy);
        cJSON_Delete(rdy);
    }

    /* Step 7: Wait for ready_ack (optional — don't fail if peripheral doesn't send it) */
    {
        cJSON *rack = recv_json(2000);
        if (rack) {
            if (!json_type_is(rack, "ready_ack")) {
                ESP_LOGW(TAG, "Expected ready_ack, got: %.32s", s_frame);
            }
            cJSON_Delete(rack);
        } else {
            ESP_LOGW(TAG, "No ready_ack (peripheral may not send one — OK)");
        }
    }

    ESP_LOGI(TAG, "PDP handshake complete");
    return ESP_OK;
}

esp_err_t pdp_tool_call(const char *tool_name, const char *call_id,
                         const char *input_json,
                         char *output, size_t output_sz)
{
    /* Build tool_call frame */
    cJSON *call = cJSON_CreateObject();
    cJSON_AddStringToObject(call, "type", "tool_call");
    cJSON_AddStringToObject(call, "id", call_id);
    cJSON_AddStringToObject(call, "tool", tool_name);

    cJSON *input = cJSON_Parse(input_json);
    if (input) {
        cJSON_AddItemToObject(call, "input", input);
    } else {
        cJSON_AddItemToObject(call, "input", cJSON_CreateObject());
    }

    esp_err_t ret = send_json(call);
    cJSON_Delete(call);

    if (ret != ESP_OK) {
        snprintf(output, output_sz, "Error: failed to send tool_call");
        return ret;
    }

    /* Wait for matching tool_result */
    cJSON *result = recv_json(MIMI_PERIPH_TOOL_TIMEOUT_MS);
    if (!result) {
        snprintf(output, output_sz, "Error: peripheral tool timeout (%s)", tool_name);
        return ESP_ERR_TIMEOUT;
    }

    if (!json_type_is(result, "tool_result")) {
        snprintf(output, output_sz, "Error: unexpected response type");
        cJSON_Delete(result);
        return ESP_FAIL;
    }

    /* Verify call ID matches */
    cJSON *id_j = cJSON_GetObjectItem(result, "id");
    if (id_j && cJSON_IsString(id_j) && strcmp(id_j->valuestring, call_id) != 0) {
        ESP_LOGW(TAG, "Call ID mismatch: expected %s got %s", call_id, id_j->valuestring);
    }

    cJSON *ok_j = cJSON_GetObjectItem(result, "ok");
    bool ok = ok_j && cJSON_IsTrue(ok_j);

    if (ok) {
        cJSON *out_j = cJSON_GetObjectItem(result, "output");
        if (out_j && cJSON_IsString(out_j)) {
            snprintf(output, output_sz, "%s", out_j->valuestring);
        } else {
            snprintf(output, output_sz, "Tool executed successfully");
        }
        ret = ESP_OK;
    } else {
        cJSON *err_j = cJSON_GetObjectItem(result, "error");
        if (err_j && cJSON_IsString(err_j)) {
            snprintf(output, output_sz, "Error: %s", err_j->valuestring);
        } else {
            snprintf(output, output_sz, "Error: tool execution failed");
        }
        ret = ESP_FAIL;
    }

    cJSON_Delete(result);
    return ret;
}
