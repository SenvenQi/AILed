#include <string.h>
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

static const char *TAG = "wifi_manager";

#define WIFI_CONNECTED BIT0
#define WIFI_FAIL      BIT1
#define MAX_RETRY      5

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_wifi_netif = NULL;
static int s_retry_num = 0;
static bool s_connected = false;
static bool s_initialized = false;
static bool s_wifi_started = false;
static bool s_connect_requested = false;

static void wifi_prepare_wait(void)
{
    if (s_wifi_event_group) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED | WIFI_FAIL);
    }
    s_retry_num = 0;
    s_connected = false;
    s_connect_requested = true;
}

static esp_err_t wifi_wait_for_result(uint32_t timeout_ms)
{
    TickType_t ticks = timeout_ms == 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED | WIFI_FAIL,
                                           pdFALSE, pdFALSE, ticks);
    if (bits & WIFI_CONNECTED) {
        return ESP_OK;
    }

    s_connect_requested = false;
    if (bits & WIFI_FAIL) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t wifi_manager_start_if_needed(void)
{
    if (!s_wifi_started) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set Wi-Fi mode");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start Wi-Fi");
        s_wifi_started = true;
    }
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_connect_requested && s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying Wi-Fi connection...");
        } else {
            s_connect_requested = false;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_connected = true;
        s_connect_requested = false;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED);
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (s_wifi_netif == NULL) {
        s_wifi_netif = esp_netif_create_default_wifi_sta();
        if (s_wifi_netif == NULL) {
            return ESP_FAIL;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to init Wi-Fi");

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                        &wifi_event_handler, NULL, &instance_any_id), TAG, "Failed to register Wi-Fi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                        &wifi_event_handler, NULL, &instance_got_ip), TAG, "Failed to register IP handler");

    s_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password, uint32_t timeout_ms)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(wifi_manager_init(), TAG, "Failed to initialize Wi-Fi manager");

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Failed to set Wi-Fi config");
    ESP_RETURN_ON_ERROR(wifi_manager_start_if_needed(), TAG, "Failed to start Wi-Fi");

    wifi_prepare_wait();
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Failed to start Wi-Fi connect");

    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s", ssid);
    return wifi_wait_for_result(timeout_ms);
}

esp_err_t wifi_manager_connect_saved(uint32_t timeout_ms)
{
    ESP_RETURN_ON_ERROR(wifi_manager_init(), TAG, "Failed to initialize Wi-Fi manager");
    ESP_RETURN_ON_ERROR(wifi_manager_start_if_needed(), TAG, "Failed to start Wi-Fi");

    wifi_prepare_wait();
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Failed to connect using saved Wi-Fi config");

    ESP_LOGI(TAG, "Connecting using saved Wi-Fi credentials");
    return wifi_wait_for_result(timeout_ms);
}

esp_err_t wifi_manager_disconnect(void)
{
    if (!s_initialized || !s_wifi_started) {
        return ESP_OK;
    }

    s_connect_requested = false;
    s_connected = false;
    return esp_wifi_disconnect();
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}