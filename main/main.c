#include "esp_log.h"

#include "application/services/app_lifecycle_service.h"
#include "infrastructure/runtime/default_runtime_ports.h"

static const char *TAG = "AILed";

/* ── Main ── */
void app_main(void)
{
    ESP_LOGI(TAG, "AILed starting...");

    app_runtime_ports_t ports = default_runtime_ports_build();
    ESP_ERROR_CHECK(app_lifecycle_start(&ports));

    ESP_LOGI(TAG, "AILed ready. Waiting for messages...");
}
