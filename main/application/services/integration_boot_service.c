#include "integration_boot_service.h"

#include "esp_check.h"

esp_err_t integration_boot_start(const integration_boot_ports_t *ports)
{
    if (!ports || !ports->init_bot || !ports->start_bot) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ports->init_bot(), "integration_boot", "init_bot failed");
    ESP_RETURN_ON_ERROR(ports->start_bot(), "integration_boot", "start_bot failed");
    return ESP_OK;
}
