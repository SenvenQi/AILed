#include "scheduler_tool.h"
#include "tool_registry.h"
#include "time_tool.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "scheduler_tool";

typedef struct {
    char *tool_name;
    char *params_str; // json string or NULL
    time_t when;
} scheduled_job_t;

static void scheduler_task_entry(void *arg)
{
    scheduled_job_t *job = (scheduled_job_t *)arg;
    if (!job) vTaskDelete(NULL);

    // compute remaining time
    time_t now = time(NULL);
    int64_t diff_sec = (int64_t)job->when - (int64_t)now;
    while (diff_sec > 0) {
        // sleep in chunks up to 60s to remain responsive
        int64_t chunk = diff_sec > 60 ? 60 : diff_sec;
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(chunk * 1000)));
        now = time(NULL);
        diff_sec = (int64_t)job->when - (int64_t)now;
    }

    ESP_LOGI(TAG, "Scheduled time reached for tool=%s", job->tool_name);

    cJSON *params = NULL;
    if (job->params_str) {
        params = cJSON_Parse(job->params_str);
    }

    cJSON *res = tool_registry_call(job->tool_name, params);
    if (res) {
        char *s = cJSON_PrintUnformatted(res);
        if (s) {
            ESP_LOGI(TAG, "Tool %s executed, result: %s", job->tool_name, s);
            free(s);
        }
        cJSON_Delete(res);
    } else {
        ESP_LOGW(TAG, "Tool %s returned NULL result", job->tool_name);
    }

    if (params) cJSON_Delete(params);
    if (job->tool_name) free(job->tool_name);
    if (job->params_str) free(job->params_str);
    free(job);
    vTaskDelete(NULL);
}

esp_err_t scheduler_schedule_tool_at(const char *tool_name, const cJSON *params_json, time_t when_seconds)
{
    if (!tool_name) return ESP_ERR_INVALID_ARG;

    // Validate tool exists
    const tool_def_t *def = tool_registry_find(tool_name);
    if (!def) return ESP_ERR_NOT_FOUND;

    scheduled_job_t *job = calloc(1, sizeof(scheduled_job_t));
    if (!job) return ESP_ERR_NO_MEM;

    job->tool_name = strdup(tool_name);
    if (params_json) {
        char *s = cJSON_PrintUnformatted(params_json);
        job->params_str = s ? strdup(s) : NULL;
        if (s) free(s);
    } else {
        job->params_str = NULL;
    }
    job->when = when_seconds;

    // Create a dedicated task to wait and call
    BaseType_t ok = xTaskCreate(scheduler_task_entry, "sched_job", 4096, job, tskIDLE_PRIORITY + 2, NULL);
    if (ok != pdPASS) {
        if (job->tool_name) free(job->tool_name);
        if (job->params_str) free(job->params_str);
        free(job);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Scheduled tool %s at %lld", tool_name, (long long)when_seconds);
    return ESP_OK;
}

// Tool handler for tool_registry: params: { "tool": string, "at": string|number }
static cJSON *handle_schedule_tool(const cJSON *params)
{
    cJSON *result = cJSON_CreateObject();
    if (!params) {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "params required");
        return result;
    }

    cJSON *tool_j = cJSON_GetObjectItem(params, "tool");
    cJSON *at_j = cJSON_GetObjectItem(params, "at");
    cJSON *p_j = cJSON_GetObjectItem(params, "params");

    if (!cJSON_IsString(tool_j) || strlen(tool_j->valuestring) == 0) {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "tool name required");
        return result;
    }

    time_t when = 0;
    time_t now = time(NULL);
    if (cJSON_IsNumber(at_j)) {
        when = (time_t)at_j->valuedouble;
    } else if (cJSON_IsString(at_j) && at_j->valuestring) {
        const char *s = at_j->valuestring;
        // support HH:MM or HH:MM:SS (today or next day if passed), or full YYYY-MM-DD HH:MM:SS
        struct tm tmv = {0};
        if (strchr(s, '-') && strchr(s, ':')) {
            // assume "YYYY-MM-DD HH:MM:SS" (use strptime if available)
            if (strptime(s, "%Y-%m-%d %H:%M:%S", &tmv) != NULL) {
                when = mktime(&tmv);
            } else if (strptime(s, "%Y-%m-%d %H:%M", &tmv) != NULL) {
                when = mktime(&tmv);
            }
        } else if (strchr(s, ':')) {
            int hr = 0, min = 0, sec = 0;
            if (sscanf(s, "%d:%d:%d", &hr, &min, &sec) >= 2) {
                struct tm now_tm;
                localtime_r(&now, &now_tm);
                now_tm.tm_hour = hr;
                now_tm.tm_min = min;
                now_tm.tm_sec = sec;
                when = mktime(&now_tm);
                if (when <= now) when += 24 * 3600; // schedule next day
            }
        }
    }

    if (when == 0) {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "invalid 'at' parameter; use unix timestamp or 'HH:MM' or 'YYYY-MM-DD HH:MM:SS'");
        return result;
    }

    esp_err_t rc = scheduler_schedule_tool_at(tool_j->valuestring, p_j, when);
    if (rc != ESP_OK) {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", esp_err_to_name(rc));
        return result;
    }

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "scheduled_unix", (double)when);
    return result;
}