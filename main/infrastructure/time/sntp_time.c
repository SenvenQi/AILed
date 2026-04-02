#include "infrastructure/time/sntp_time.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <time.h>
#include <esp_sntp.h>
#include "esp_timer.h"

static const char *TAG = "time/sntp";
static EventGroupHandle_t s_time_event_group = NULL;
#define TIME_SYNC_BIT BIT0

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP: time synchronized");
	//打印当前时间
	time_t now = tv->tv_sec;
	struct tm timeinfo;
	gmtime_r(&now, &timeinfo);
	char strftime_buf[64];
	strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
	ESP_LOGI(TAG, "Current time: %s", strftime_buf);
    if (s_time_event_group) {
        xEventGroupSetBits(s_time_event_group, TIME_SYNC_BIT);
    }

    /* 首次同步后启动内部 demo 任务（只启动一次） */
    static bool s_demo_started = false;
    if (!s_demo_started) {
        s_demo_started = true;
        /* create demo task */
        extern void sntp_time_start_task(void);
        sntp_time_start_task();
    }
}

esp_err_t time_sntp_init(const char *server)
{
    if (s_time_event_group == NULL) {
        s_time_event_group = xEventGroupCreate();
        if (s_time_event_group == NULL) {
            ESP_LOGE(TAG, "Failed to create event group");
            return ESP_ERR_NO_MEM;
        }
    }
	setenv("TZ", "CST-8", 1);
	tzset();

    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    if (server != NULL && server[0] != '\0') {
        esp_sntp_setservername(0, server);
        ESP_LOGI(TAG, "SNTP server set: %s", server);
    } else {
        esp_sntp_setservername(0, "pool.ntp.org");
        ESP_LOGI(TAG, "SNTP server set: pool.ntp.org");
    }
    // sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP initialized");
    return ESP_OK;
}

esp_err_t time_sntp_stop(void)
{
    esp_sntp_stop();
    if (s_time_event_group) {
        vEventGroupDelete(s_time_event_group);
        s_time_event_group = NULL;
    }
    ESP_LOGI(TAG, "SNTP stopped");
    return ESP_OK;
}

bool time_is_synchronized(void)
{
    time_t now = 0;
    time(&now);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    return (timeinfo.tm_year + 1900) > 2016;
}

esp_err_t time_wait_for_sync(uint32_t timeout_ms)
{
    if (s_time_event_group == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_time_event_group, TIME_SYNC_BIT, pdTRUE, pdFALSE, ticks);
    if (bits & TIME_SYNC_BIT) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t time_wait_for_next_second(uint32_t timeout_ms)
{
    if (!time_is_synchronized()) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint64_t start_us = esp_timer_get_time();
    const uint64_t timeout_us = (uint64_t)timeout_ms * 1000ULL;

    while (1) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t now_us = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
        uint64_t next_sec_us = ((uint64_t)tv.tv_sec + 1ULL) * 1000000ULL;

        if (now_us >= next_sec_us) {
            return ESP_OK;
        }

        if ((esp_timer_get_time() - start_us) >= timeout_us) {
            return ESP_ERR_TIMEOUT;
        }

        uint64_t remain_us = next_sec_us - now_us;

        /* 直接计算到整秒的毫秒数，先做粗延时到接近整秒（留 5ms 余量） */
        if (remain_us > 5000ULL) {
            uint32_t delay_ms = (uint32_t)((remain_us - 5000ULL) / 1000ULL);
            if (delay_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
            }
            continue;
        }

        /* 剩余 <=5ms，使用微秒延时并短轮询，保证尽可能对齐 */
        if (remain_us > 1000ULL) {
            /* 留余量 500us 以启动轮询 */
            uint32_t d = (uint32_t)(remain_us - 500ULL);
            esp_rom_delay_us(d);
        }

        while (1) {
            struct timeval tv2;
            gettimeofday(&tv2, NULL);
            uint64_t now2_us = (uint64_t)tv2.tv_sec * 1000000ULL + (uint64_t)tv2.tv_usec;
            if (now2_us >= next_sec_us) break;
            taskYIELD();
            if ((esp_timer_get_time() - start_us) >= timeout_us) {
                return ESP_ERR_TIMEOUT;
            }
        }
        return ESP_OK;
    }
}

void time_sleep_seconds(uint32_t seconds)
{
    if (seconds == 0) return;
    /* 使用 vTaskDelay 做粗粒度休眠，达到秒级精度 */
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000U));
}

/* ---------------- task (moved from sntp_demo) ---------------- */
static void sntp_task(void *arg)
{
    ESP_LOGI(TAG, "Demo task waiting for initial second alignment...");
    if (time_wait_for_next_second(5000) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to align to next second (timeout)");
    }

    while (1) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        time_t now = tv.tv_sec;
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI(TAG, "Tick: %s.%03u", buf, (unsigned)(tv.tv_usec / 1000));

        /* 等待下一个整秒 */
        if (time_wait_for_next_second(2000) != ESP_OK) {
            /* 如果等待超时，则退回到粗粒度延时，避免紧循环 */
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void sntp_time_start_task(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(sntp_task, "sntp_demo", 4096, NULL, tskIDLE_PRIORITY + 1, NULL, tskNO_AFFINITY);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "Failed to create demo task");
    }
}
