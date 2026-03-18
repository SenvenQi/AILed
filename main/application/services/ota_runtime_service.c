#include "ota_runtime_service.h"

#include "domain/ota/ota_policy.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "ota_runtime";

esp_err_t ota_runtime_boot(const ota_runtime_ports_t *ports)
{
    if (!ota_policy_is_enabled()) {
        ESP_LOGI(TAG, "OTA feature is disabled by domain policy");
        return ESP_OK;
    }

    if (!ports || !ports->init_ota) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ports->init_ota(), TAG, "init_ota failed");
    ESP_LOGI(TAG, "OTA feature is enabled and initialized");
    return ESP_OK;
}
