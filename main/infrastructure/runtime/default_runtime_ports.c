#include "default_runtime_ports.h"
#include "domain/ports/app_runtime_ports.h"
#include "domain/ports/agent_runtime_ports.h"
#include "domain/ports/ota_runtime_ports.h"
#include "application/services/agent_runtime_service.h"
#include "esp_check.h"
#include "feishu_bot.h"
#include "infrastructure/runtime/default_agent_runtime_ports.h"
#include "infrastructure/runtime/default_ota_runtime_ports.h"
#include "infrastructure/startup/system_startup.h"
#include "led_strip_tool.h"

static esp_err_t port_init_nvs(void)
{
    return system_startup_init_nvs();
}

static esp_err_t port_init_device_stack(void)
{
    return system_startup_init_led_tools_bus();
}

static esp_err_t port_ensure_network_ready(void)
{
    return system_startup_ensure_wifi_ready();
}

static esp_err_t port_start_ai_runtime(void)
{
    agent_runtime_ports_t ports = default_agent_runtime_ports_build();
    return agent_runtime_service_start(&ports);
}

static esp_err_t port_init_ota(void)
{
    ota_runtime_ports_t ports = default_ota_runtime_ports_build();
    return ports.init_ota ? ports.init_ota() : ESP_ERR_INVALID_ARG;
}

static esp_err_t port_publish_system_context(void)
{
    return system_startup_publish_agent_system_context();
}

static esp_err_t port_init_external_channels(void)
{
    return feishu_bot_init();
}

static esp_err_t port_start_external_channels(void)
{
    ESP_RETURN_ON_ERROR(feishu_bot_start(), "runtime_ports", "feishu start failed");
    return ESP_OK;
}

static esp_err_t port_clear_status_indicator(void)
{
    return led_strip_tool_clear();
}

app_runtime_ports_t default_runtime_ports_build(void)
{
    app_runtime_ports_t ports = {
        .init_nvs = port_init_nvs,
        .init_device_stack = port_init_device_stack,
        .ensure_network_ready = port_ensure_network_ready,
        .init_ota = port_init_ota,
        .start_ai_runtime = port_start_ai_runtime,
        .publish_system_context = port_publish_system_context,
        .init_external_channels = port_init_external_channels,
        .start_external_channels = port_start_external_channels,
        .clear_status_indicator = port_clear_status_indicator,
    };

    return ports;
}
