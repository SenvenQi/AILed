#pragma once
#include "esp_err.h"
typedef struct {
    esp_err_t (*init_nvs)(void);
    esp_err_t (*init_device_stack)(void);
    esp_err_t (*ensure_network_ready)(void);
    esp_err_t (*init_ota)(void);
    esp_err_t (*start_ai_runtime)(void);
    esp_err_t (*publish_system_context)(void);
    esp_err_t (*init_external_channels)(void);
    esp_err_t (*start_external_channels)(void);
    esp_err_t (*clear_status_indicator)(void);
} app_runtime_ports_t;
