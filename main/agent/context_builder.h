#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Build the system prompt from bootstrap files (SOUL.md, USER.md)
 * and memory context (MEMORY.md + recent daily notes).
 *
 * @param buf   Output buffer (caller allocates, recommend MIMI_CONTEXT_BUF_SIZE)
 * @param size  Buffer size
 */
esp_err_t context_build_system_prompt(char *buf, size_t size);

/**
 * Set the connected peripheral info string injected into the system prompt.
 * Called by peripheral_manager when a peripheral connects.
 * Pass NULL or empty string to clear.
 *
 * @param info  Multi-line description of the connected peripheral, or NULL to clear.
 *              Example:
 *              "## Connected Peripheral: 6-DOF Robotic Arm (robotic_arm v1.0.0)\n"
 *              "Description: ...\nAvailable tools: arm_move, arm_gripper, arm_status\n"
 */
void context_builder_set_peripheral_info(const char *info);

/**
 * Clear the peripheral info (called on disconnect).
 */
void context_builder_clear_peripheral_info(void);
