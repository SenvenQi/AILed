// Simple one-shot scheduler tool to run other registered tools at a given wall-clock time.
#ifndef MAIN_INFRASTRUCTURE_TOOLS_SCHEDULER_TOOL_H
#define MAIN_INFRASTRUCTURE_TOOLS_SCHEDULER_TOOL_H

#include "esp_err.h"
#include "cJSON.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Programmatic API: schedule a tool by name to run at unix timestamp 'when_seconds'.
// 'params_json' may be NULL; it will be deep-copied internally. Returns ESP_OK on success.
esp_err_t scheduler_schedule_tool_at(const char *tool_name, const cJSON *params_json, time_t when_seconds);

#ifdef __cplusplus
}
#endif

#endif // MAIN_INFRASTRUCTURE_TOOLS_SCHEDULER_TOOL_H
