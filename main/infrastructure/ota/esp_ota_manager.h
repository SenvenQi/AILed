#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    ESP_OTA_MANAGER_STATE_IDLE = 0,
    ESP_OTA_MANAGER_STATE_RUNNING,
    ESP_OTA_MANAGER_STATE_SUCCESS,
    ESP_OTA_MANAGER_STATE_FAILED,
} esp_ota_manager_state_t;

typedef struct {
    const char *default_url;
    bool auto_reboot;
    bool skip_common_name_check;
    uint32_t task_stack_size;
    uint32_t task_priority;
} esp_ota_manager_config_t;

esp_err_t esp_ota_manager_init(const esp_ota_manager_config_t *config);
esp_err_t esp_ota_manager_start(const char *url);
esp_ota_manager_state_t esp_ota_manager_get_state(void);
bool esp_ota_manager_is_busy(void);
esp_err_t esp_ota_manager_get_last_error(void);
const char *esp_ota_manager_get_last_message(void);
