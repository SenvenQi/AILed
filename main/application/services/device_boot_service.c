#include "device_boot_service.h"

#include "domain/device/indicator_policy.h"
#include "esp_check.h"

esp_err_t device_boot_finalize(const device_boot_ports_t *ports)
{
    if (!ports || !ports->clear_status_indicator) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!device_indicator_should_clear_after_boot()) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ports->clear_status_indicator(), "device_boot", "clear_status_indicator failed");
    return ESP_OK;
}
