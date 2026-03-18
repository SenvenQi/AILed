#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "config_mode.h"
#include "wifi_manager.h"
#include "led_strip_tool.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

static const char *TAG = "config_mode";

#define CONFIG_MODE_CONNECTED_BIT   BIT0
#define CONFIG_MODE_SERVICE_NAME_LEN 32
#define CONFIG_MODE_DEFAULT_PREFIX   "AILed"
#define CONFIG_MODE_DEFAULT_POP      "ailed-ble"
#define CONFIG_MODE_DEFAULT_TIMEOUT_MS 15000
#define CONFIG_MODE_LED_BLINK_MS      300

static struct {
    char service_name_prefix[16];
    char proof_of_possession[32];
    char service_name[CONFIG_MODE_SERVICE_NAME_LEN];
    uint32_t wifi_connect_timeout_ms;
    EventGroupHandle_t events;
    bool initialized;
    bool handlers_registered;
    bool provisioning_active;
    bool status_led_running;
    TaskHandle_t status_led_task;
} s_config_mode;

static void config_mode_status_led_task(void *arg)
{
    (void)arg;
    bool on = false;

    while (s_config_mode.status_led_running) {
        if (!wifi_manager_is_connected()) {
            on = !on;
            if (on) {
                led_strip_tool_fill(255, 0, 0);
            } else {
                led_strip_tool_clear();
            }
        } else if (on) {
            led_strip_tool_clear();
            on = false;
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_MODE_LED_BLINK_MS));
    }

    s_config_mode.status_led_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t config_mode_status_led_start(void)
{
    if (s_config_mode.status_led_running) {
        return ESP_OK;
    }

    s_config_mode.status_led_running = true;
    BaseType_t ok = xTaskCreate(config_mode_status_led_task,
                                "cfg_led",
                                2048,
                                NULL,
                                3,
                                &s_config_mode.status_led_task);
    if (ok != pdPASS) {
        s_config_mode.status_led_running = false;
        s_config_mode.status_led_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void config_mode_status_led_stop(void)
{
    if (!s_config_mode.status_led_running) {
        return;
    }

    s_config_mode.status_led_running = false;
    for (int i = 0; i < 25 && s_config_mode.status_led_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void config_mode_status_led_show_success(void)
{
    led_strip_tool_fill(0, 255, 0);
}

static esp_err_t config_mode_store_credentials(const wifi_sta_config_t *wifi_cfg)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_MODE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, CONFIG_MODE_NVS_KEY_SSID, (const char *)wifi_cfg->ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, CONFIG_MODE_NVS_KEY_PASS, (const char *)wifi_cfg->password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void config_mode_build_service_name(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_config_mode.service_name, sizeof(s_config_mode.service_name), "%s_%02X%02X%02X",
             s_config_mode.service_name_prefix[0] ? s_config_mode.service_name_prefix : CONFIG_MODE_DEFAULT_PREFIX,
             mac[3], mac[4], mac[5]);
}

static void config_mode_event_handler(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "BLE provisioning started, service=%s", s_config_mode.service_name);
            break;
        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *wifi_cfg = (wifi_sta_config_t *)event_data;
            if (wifi_cfg) {
                ESP_LOGI(TAG, "Received Wi-Fi credentials for SSID: %s", (const char *)wifi_cfg->ssid);
                if (config_mode_store_credentials(wifi_cfg) != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to mirror Wi-Fi credentials to custom NVS namespace");
                }
            }
            break;
        }
        case WIFI_PROV_CRED_FAIL:
            ESP_LOGW(TAG, "Provisioning credentials failed, waiting for retry");
            wifi_prov_mgr_reset_sm_state_on_failure();
            break;
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning credentials accepted");
            break;
        case WIFI_PROV_END:
            ESP_LOGI(TAG, "Provisioning service stopped");
            s_config_mode.provisioning_active = false;
            wifi_prov_mgr_deinit();
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_config_mode.events, CONFIG_MODE_CONNECTED_BIT);
    }
}

static esp_err_t config_mode_register_handlers(void)
{
    if (s_config_mode.handlers_registered) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                        &config_mode_event_handler, NULL), TAG, "Failed to register provisioning handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                        &config_mode_event_handler, NULL), TAG, "Failed to register IP handler");
    s_config_mode.handlers_registered = true;
    return ESP_OK;
}

static esp_err_t config_mode_start_provisioning(bool reprovision)
{
    wifi_prov_mgr_config_t prov_config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };

    ESP_RETURN_ON_ERROR(wifi_manager_disconnect(), TAG, "Failed to disconnect Wi-Fi before provisioning");
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_init(prov_config), TAG, "Failed to init Wi-Fi provisioning manager");

    if (reprovision) {
        ESP_RETURN_ON_ERROR(wifi_prov_mgr_reset_sm_state_for_reprovision(), TAG,
                            "Failed to reset provisioning state for reprovision");
    }

    xEventGroupClearBits(s_config_mode.events, CONFIG_MODE_CONNECTED_BIT);
    s_config_mode.provisioning_active = true;
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1,
                        s_config_mode.proof_of_possession,
                        s_config_mode.service_name,
                        NULL), TAG, "Failed to start BLE provisioning");

    xEventGroupWaitBits(s_config_mode.events,
                        CONFIG_MODE_CONNECTED_BIT,
                        pdTRUE,
                        pdFALSE,
                        portMAX_DELAY);

    if (s_config_mode.provisioning_active) {
        wifi_prov_mgr_stop_provisioning();
    }

    ESP_LOGI(TAG, "Provisioning completed and Wi-Fi connected");
    return ESP_OK;
}

esp_err_t config_mode_init(const config_mode_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_config_mode.initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(wifi_manager_init(), TAG, "Failed to initialize Wi-Fi manager");

    s_config_mode.events = xEventGroupCreate();
    if (s_config_mode.events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(s_config_mode.service_name_prefix,
            (config->service_name_prefix && config->service_name_prefix[0]) ? config->service_name_prefix : CONFIG_MODE_DEFAULT_PREFIX,
            sizeof(s_config_mode.service_name_prefix) - 1);
    strncpy(s_config_mode.proof_of_possession,
            (config->proof_of_possession && config->proof_of_possession[0]) ? config->proof_of_possession : CONFIG_MODE_DEFAULT_POP,
            sizeof(s_config_mode.proof_of_possession) - 1);
    s_config_mode.wifi_connect_timeout_ms = config->wifi_connect_timeout_ms ?
                                            config->wifi_connect_timeout_ms : CONFIG_MODE_DEFAULT_TIMEOUT_MS;

    config_mode_build_service_name();
    ESP_RETURN_ON_ERROR(config_mode_register_handlers(), TAG, "Failed to register config mode handlers");

    s_config_mode.initialized = true;
    ESP_LOGI(TAG, "Config mode initialized, BLE service=%s", s_config_mode.service_name);
    return ESP_OK;
}

bool config_mode_is_configured(void)
{
    bool provisioned = false;
    if (wifi_prov_mgr_is_provisioned(&provisioned) != ESP_OK) {
        return false;
    }
    return provisioned;
}

esp_err_t config_mode_clear_wifi_config(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_MODE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_erase_key(nvs, CONFIG_MODE_NVS_KEY_SSID);
        nvs_erase_key(nvs, CONFIG_MODE_NVS_KEY_PASS);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_RETURN_ON_ERROR(esp_wifi_restore(), TAG, "Failed to clear Wi-Fi config from driver storage");
    return ESP_OK;
}

esp_err_t config_mode_ensure_wifi_connected(void)
{
    ESP_RETURN_ON_ERROR(config_mode_status_led_start(), TAG, "Failed to start status LED task");

    bool configured = config_mode_is_configured();

    if (configured) {
        esp_err_t err = wifi_manager_connect_saved(s_config_mode.wifi_connect_timeout_ms);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Connected using saved Wi-Fi configuration");
            config_mode_status_led_stop();
            config_mode_status_led_show_success();
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Saved Wi-Fi configuration failed, entering BLE provisioning mode");
        err = config_mode_start_provisioning(true);
        config_mode_status_led_stop();
        if (err == ESP_OK) {
            config_mode_status_led_show_success();
        }
        return err;
    }

    ESP_LOGI(TAG, "No Wi-Fi configuration found, entering BLE provisioning mode");
    esp_err_t err = config_mode_start_provisioning(false);
    config_mode_status_led_stop();
    if (err == ESP_OK) {
        config_mode_status_led_show_success();
    }
    return err;
}

const char *config_mode_get_service_name(void)
{
    return s_config_mode.service_name;
}
