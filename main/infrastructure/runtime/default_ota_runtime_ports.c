#include "default_ota_runtime_ports.h"
#include "domain/ports/ota_runtime_ports.h"
#include "infrastructure/ota/esp_ota_manager.h"

static esp_err_t port_init_ota(void)
{
    esp_ota_manager_config_t cfg = {
        .default_url = NULL,
        .auto_reboot = true,
        .skip_common_name_check = false,
        .task_stack_size = 0,
        .task_priority = 0,
    };
    return esp_ota_manager_init(&cfg);
}

ota_runtime_ports_t default_ota_runtime_ports_build(void)
{
    ota_runtime_ports_t ports = {
        .init_ota = port_init_ota,
    };
    return ports;
}
