#include "system_startup.h"

#include <stdio.h>
#include <stdlib.h>

#include "app_config.h"
#include "cJSON.h"
#include "config_mode.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "led_strip_tool.h"
#include "message_bus.h"
#include "nvs_flash.h"
#include "tool_registry.h"

static const char *TAG = "system_startup";

static char *read_text_file_alloc(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *buf = calloc(1, (size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';
    return buf;
}

static esp_err_t startup_mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 6,
        .format_if_mount_failed = false,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t total = 0;
    size_t used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted: total=%u used=%u", (unsigned)total, (unsigned)used);
    }
    return ESP_OK;
}

esp_err_t system_startup_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Failed to erase NVS");
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t system_startup_init_led_tools_bus(void)
{
    ESP_RETURN_ON_ERROR(led_strip_tool_init(APP_LED_GPIO, APP_LED_COUNT), TAG, "Failed to init LED strip");

    tool_registry_init();
    tool_registry_register_builtins();

    esp_err_t spiffs_ret = startup_mount_spiffs();
    if (spiffs_ret == ESP_OK) {
        esp_err_t cfg_ret = tool_registry_load_config_from_file("/spiffs/config/SKILLS.md");
        if (cfg_ret != ESP_OK && cfg_ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Load /spiffs/config/SKILLS.md failed: %s", esp_err_to_name(cfg_ret));
        }
    } else {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(spiffs_ret));
    }

    ESP_LOGI(TAG, "Tools registered: %d", tool_registry_count());

    ESP_RETURN_ON_ERROR(msg_bus_init(16), TAG, "Failed to init message bus");
    return ESP_OK;
}

esp_err_t system_startup_ensure_wifi_ready(void)
{
    config_mode_config_t cfg = app_make_config_mode_config();
    ESP_RETURN_ON_ERROR(config_mode_init(&cfg), TAG, "Failed to init config mode");
    ESP_RETURN_ON_ERROR(config_mode_ensure_wifi_connected(), TAG, "Failed to ensure Wi-Fi connected");
    return ESP_OK;
}

esp_err_t system_startup_publish_agent_system_context(void)
{
    char *soul = read_text_file_alloc("/spiffs/config/SOUL.md");
    char *user = read_text_file_alloc("/spiffs/config/USER.md");

    if (!soul && !user) {
        ESP_LOGW(TAG, "No SOUL.md or USER.md found in SPIFFS");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *payload = cJSON_CreateObject();
    if (!payload) {
        free(soul);
        free(user);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(payload, "kind", "agent_system_context");
    cJSON_AddStringToObject(payload, "soul", soul ? soul : "");
    cJSON_AddStringToObject(payload, "user", user ? user : "");

    char *payload_str = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    free(soul);
    free(user);

    if (!payload_str) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = msg_bus_publish(MSG_TYPE_SYSTEM, payload_str, 1000);
    free(payload_str);
    return ret;
}
