// Simple time utility wrappers for project-wide use
#ifndef MAIN_INFRASTRUCTURE_TOOLS_TIME_TOOL_H
#define MAIN_INFRASTRUCTURE_TOOLS_TIME_TOOL_H

#include "esp_err.h"
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Get current time as time_t. Returns ESP_OK on success.
esp_err_t tools_get_time(time_t *out_time);

// Get current time as struct timeval. Returns ESP_OK on success.
esp_err_t tools_gettimeofday(struct timeval *tv);

// Format a time_t into an ISO-like string "YYYY-MM-DD HH:MM:SS".
// If utc is true, formats as UTC; otherwise uses localtime.
// Returns ESP_OK on success, ESP_ERR_INVALID_ARG for bad args, ESP_FAIL if buffer too small.
esp_err_t tools_format_time_iso(time_t t, char *buf, size_t buf_len, bool utc);

#ifdef __cplusplus
}
#endif

#endif // MAIN_INFRASTRUCTURE_TOOLS_TIME_TOOL_H
