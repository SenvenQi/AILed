#include "app_lifecycle_service.h"

#include "application/ports/device_boot_ports.h"
#include "application/ports/integration_boot_ports.h"
#include "application/services/ddd_regression_checks.h"
#include "application/services/device_boot_service.h"
#include "application/services/integration_boot_service.h"
#include "application/services/ota_runtime_service.h"
#include "domain/system/boot_policy.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "app_lifecycle";

esp_err_t app_lifecycle_start(const app_runtime_ports_t *ports)
{
    if (!ports || !ports->init_nvs || !ports->init_device_stack ||
        !ports->ensure_network_ready || !ports->init_ota || !ports->start_ai_runtime ||
        !ports->init_external_channels || !ports->start_external_channels ||
        !ports->clear_status_indicator ||
        !ports->publish_system_context) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ddd_regression_checks_run(), TAG, "ddd_regression_checks_run failed");

    ESP_RETURN_ON_ERROR(ports->init_nvs(), TAG, "init_nvs failed");
    ESP_RETURN_ON_ERROR(ports->init_device_stack(), TAG, "init_device_stack failed");
    ESP_RETURN_ON_ERROR(ports->ensure_network_ready(), TAG, "ensure_network_ready failed");

    ota_runtime_ports_t ota_ports = {
        .init_ota = ports->init_ota,
    };
    ESP_RETURN_ON_ERROR(ota_runtime_boot(&ota_ports), TAG, "ota_runtime_boot failed");

    ESP_RETURN_ON_ERROR(ports->start_ai_runtime(), TAG, "start_ai_runtime failed");

    esp_err_t ctx_err = ports->publish_system_context();
    if (ctx_err != ESP_OK && !boot_policy_allow_context_publish_failure()) {
        ESP_RETURN_ON_ERROR(ctx_err, TAG, "publish_system_context failed");
    }

    integration_boot_ports_t integration_ports = {
        .init_bot = ports->init_external_channels,
        .start_bot = ports->start_external_channels,
    };

    ESP_RETURN_ON_ERROR(integration_boot_start(&integration_ports), TAG, "integration_boot_start failed");

    device_boot_ports_t device_ports = {
        .clear_status_indicator = ports->clear_status_indicator,
    };

    ESP_RETURN_ON_ERROR(device_boot_finalize(&device_ports), TAG, "device_boot_finalize failed");

    ESP_LOGI(TAG, "Application lifecycle boot completed");
    return ESP_OK;
}
