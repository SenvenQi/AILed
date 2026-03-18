#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CONFIG_MODE_NVS_NAMESPACE   "wifi_cfg"
#define CONFIG_MODE_NVS_KEY_SSID    "ssid"
#define CONFIG_MODE_NVS_KEY_PASS    "pass"

typedef struct {
	const char *service_name_prefix;
	const char *proof_of_possession;
	uint32_t wifi_connect_timeout_ms;
} config_mode_config_t;

esp_err_t config_mode_init(const config_mode_config_t *config);
esp_err_t config_mode_ensure_wifi_connected(void);
bool config_mode_is_configured(void);
esp_err_t config_mode_clear_wifi_config(void);
const char *config_mode_get_service_name(void);
