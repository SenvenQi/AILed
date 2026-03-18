#pragma once

#include "esp_err.h"

esp_err_t system_startup_init_nvs(void);
esp_err_t system_startup_init_led_tools_bus(void);
esp_err_t system_startup_ensure_wifi_ready(void);
esp_err_t system_startup_publish_agent_system_context(void);
