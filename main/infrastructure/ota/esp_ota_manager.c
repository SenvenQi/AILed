#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "infrastructure/ota/esp_ota_manager.h"

static const char *TAG = "esp_ota_mgr";

#define OTA_DEFAULT_TASK_STACK_SIZE   (8 * 1024)
#define OTA_DEFAULT_TASK_PRIORITY     4
#define OTA_STATUS_MSG_LEN            160

typedef struct {
    char *default_url;
    bool auto_reboot;
    bool skip_common_name_check;
    uint32_t task_stack_size;
    uint32_t task_priority;
    esp_ota_manager_state_t state;
    esp_err_t last_error;
    char last_message[OTA_STATUS_MSG_LEN];
    SemaphoreHandle_t lock;
    TaskHandle_t task_handle;
    bool initialized;
} esp_ota_manager_ctx_t;

typedef struct {
    char *url;
} ota_job_t;

static esp_ota_manager_ctx_t s_ota = {
    .task_stack_size = OTA_DEFAULT_TASK_STACK_SIZE,
    .task_priority = OTA_DEFAULT_TASK_PRIORITY,
    .state = ESP_OTA_MANAGER_STATE_IDLE,
    .last_error = ESP_OK,
};

static void ota_lock(void)
{
    if (s_ota.lock) {
        xSemaphoreTake(s_ota.lock, portMAX_DELAY);
    }
}

static void ota_unlock(void)
{
    if (s_ota.lock) {
        xSemaphoreGive(s_ota.lock);
    }
}

static void ota_set_status(esp_ota_manager_state_t state, esp_err_t err, const char *fmt, ...)
{
    ota_lock();
    s_ota.state = state;
    s_ota.last_error = err;

    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(s_ota.last_message, sizeof(s_ota.last_message), fmt, args);
        va_end(args);
    } else {
        s_ota.last_message[0] = '\0';
    }

    ota_unlock();
}

static esp_err_t ota_perform(const char *url)
{
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = s_ota.skip_common_name_check,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    ESP_LOGI(TAG, "Starting OTA from %s", url);
    return esp_https_ota(&ota_config);
}

static void ota_task(void *arg)
{
    ota_job_t *job = (ota_job_t *)arg;
    esp_err_t err = ESP_FAIL;

    if (job == NULL || job->url == NULL) {
        ota_set_status(ESP_OTA_MANAGER_STATE_FAILED, ESP_ERR_INVALID_ARG, "OTA job missing URL");
        goto cleanup;
    }

    ota_set_status(ESP_OTA_MANAGER_STATE_RUNNING, ESP_OK, "OTA downloading firmware");
    err = ota_perform(job->url);
    if (err == ESP_OK) {
        ota_set_status(ESP_OTA_MANAGER_STATE_SUCCESS, ESP_OK, "OTA completed successfully");
        ESP_LOGI(TAG, "OTA completed successfully");

        if (s_ota.auto_reboot) {
            ESP_LOGI(TAG, "Rebooting after OTA success");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
    } else {
        ota_set_status(ESP_OTA_MANAGER_STATE_FAILED, err, "OTA failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    }

cleanup:
    ota_lock();
    s_ota.task_handle = NULL;
    ota_unlock();

    if (job) {
        free(job->url);
        free(job);
    }
    vTaskDelete(NULL);
}

esp_err_t esp_ota_manager_init(const esp_ota_manager_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ota.lock == NULL) {
        s_ota.lock = xSemaphoreCreateMutex();
        if (s_ota.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    ota_lock();

    if (s_ota.task_handle != NULL) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    free(s_ota.default_url);
    s_ota.default_url = config->default_url ? strdup(config->default_url) : NULL;
    if (config->default_url && s_ota.default_url == NULL) {
        ota_unlock();
        return ESP_ERR_NO_MEM;
    }

    s_ota.auto_reboot = config->auto_reboot;
    s_ota.skip_common_name_check = config->skip_common_name_check;
    s_ota.task_stack_size = config->task_stack_size ? config->task_stack_size : OTA_DEFAULT_TASK_STACK_SIZE;
    s_ota.task_priority = config->task_priority ? config->task_priority : OTA_DEFAULT_TASK_PRIORITY;
    s_ota.state = ESP_OTA_MANAGER_STATE_IDLE;
    s_ota.last_error = ESP_OK;
    snprintf(s_ota.last_message, sizeof(s_ota.last_message), "OTA manager ready");
    s_ota.initialized = true;

    ota_unlock();

    ESP_LOGI(TAG, "OTA manager initialized");
    return ESP_OK;
}

esp_err_t esp_ota_manager_start(const char *url)
{
    ota_job_t *job = NULL;
    const char *resolved_url = NULL;

    ota_lock();

    if (!s_ota.initialized) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ota.task_handle != NULL || s_ota.state == ESP_OTA_MANAGER_STATE_RUNNING) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    resolved_url = (url && url[0]) ? url : s_ota.default_url;
    if (resolved_url == NULL || resolved_url[0] == '\0') {
        ota_unlock();
        return ESP_ERR_INVALID_ARG;
    }

    job = calloc(1, sizeof(*job));
    if (job == NULL) {
        ota_unlock();
        return ESP_ERR_NO_MEM;
    }

    job->url = strdup(resolved_url);
    if (job->url == NULL) {
        free(job);
        ota_unlock();
        return ESP_ERR_NO_MEM;
    }

    s_ota.state = ESP_OTA_MANAGER_STATE_RUNNING;
    s_ota.last_error = ESP_OK;
    snprintf(s_ota.last_message, sizeof(s_ota.last_message), "OTA scheduled");

    BaseType_t ok = xTaskCreate(ota_task,
                                "ota_manager",
                                s_ota.task_stack_size,
                                job,
                                (UBaseType_t)s_ota.task_priority,
                                &s_ota.task_handle);
    ota_unlock();

    if (ok != pdPASS) {
        free(job->url);
        free(job);
        ota_set_status(ESP_OTA_MANAGER_STATE_FAILED, ESP_ERR_NO_MEM, "Failed to create OTA task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_ota_manager_state_t esp_ota_manager_get_state(void)
{
    esp_ota_manager_state_t state;
    ota_lock();
    state = s_ota.state;
    ota_unlock();
    return state;
}

bool esp_ota_manager_is_busy(void)
{
    return esp_ota_manager_get_state() == ESP_OTA_MANAGER_STATE_RUNNING;
}

esp_err_t esp_ota_manager_get_last_error(void)
{
    esp_err_t err;
    ota_lock();
    err = s_ota.last_error;
    ota_unlock();
    return err;
}

const char *esp_ota_manager_get_last_message(void)
{
    return s_ota.last_message;
}
