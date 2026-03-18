#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_connect(const char *ssid, const char *password, uint32_t timeout_ms);
esp_err_t wifi_manager_connect_saved(uint32_t timeout_ms);
esp_err_t wifi_manager_disconnect(void);
bool wifi_manager_is_connected(void);