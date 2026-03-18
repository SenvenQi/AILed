#include <string.h>
#include <stdlib.h>
#include "message_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "msg_bus";

#define MSG_BUS_TASK_STACK  (8 * 1024)
#define MSG_BUS_TASK_PRIO   5
#define MSG_BUS_SUB_MAX     16

struct msg_bus_sub {
    msg_type_t type;
    msg_bus_handler_t handler;
    void *user_data;
    bool active;
};

static struct {
    QueueHandle_t queue;
    struct msg_bus_sub subs[MSG_BUS_SUB_MAX];
    int sub_count;
    TaskHandle_t task;
    bool initialized;
} s_bus = {
    .initialized = false,
};

static void msg_bus_dispatch_task(void *arg)
{
    (void)arg;
    msg_bus_msg_t msg;

    ESP_LOGI(TAG, "Dispatch task started");

    for (;;) {
        if (xQueueReceive(s_bus.queue, &msg, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < s_bus.sub_count; i++) {
                if (s_bus.subs[i].active && s_bus.subs[i].type == msg.type) {
                    s_bus.subs[i].handler(&msg, s_bus.subs[i].user_data);
                }
            }
            free(msg.data);
        }
    }
}

esp_err_t msg_bus_init(int queue_size)
{
    if (s_bus.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (queue_size <= 0) {
        queue_size = 16;
    }

    s_bus.queue = xQueueCreate(queue_size, sizeof(msg_bus_msg_t));
    if (s_bus.queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_ERR_NO_MEM;
    }

    s_bus.sub_count = 0;
    memset(s_bus.subs, 0, sizeof(s_bus.subs));

    BaseType_t ret = xTaskCreateWithCaps(msg_bus_dispatch_task, "msg_bus",
                                 MSG_BUS_TASK_STACK, NULL,
                                 MSG_BUS_TASK_PRIO, &s_bus.task,
                                 MALLOC_CAP_SPIRAM);
    if (ret != pdPASS) {
        vQueueDelete(s_bus.queue);
        ESP_LOGE(TAG, "Failed to create dispatch task");
        return ESP_FAIL;
    }

    s_bus.initialized = true;
    ESP_LOGI(TAG, "Message bus initialized, queue_size=%d", queue_size);
    return ESP_OK;
}

esp_err_t msg_bus_publish(msg_type_t type, const char *data, uint32_t timeout_ms)
{
    if (!s_bus.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (type >= MSG_TYPE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    msg_bus_msg_t msg = {
        .type = type,
        .data = data ? strdup(data) : NULL,
        .data_len = data ? strlen(data) : 0,
        .timestamp = esp_timer_get_time() / 1000,
    };

    if (data && msg.data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    TickType_t ticks = (timeout_ms == portMAX_DELAY)
                       ? portMAX_DELAY
                       : pdMS_TO_TICKS(timeout_ms);

    if (xQueueSend(s_bus.queue, &msg, ticks) != pdTRUE) {
        free(msg.data);
        ESP_LOGW(TAG, "Queue full, message dropped (type=%d)", type);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

msg_bus_sub_handle_t msg_bus_subscribe(msg_type_t type, msg_bus_handler_t handler, void *user_data)
{
    if (!s_bus.initialized || handler == NULL || type >= MSG_TYPE_MAX) {
        return NULL;
    }
    if (s_bus.sub_count >= MSG_BUS_SUB_MAX) {
        ESP_LOGE(TAG, "Subscriber limit reached");
        return NULL;
    }

    struct msg_bus_sub *sub = &s_bus.subs[s_bus.sub_count];
    sub->type = type;
    sub->handler = handler;
    sub->user_data = user_data;
    sub->active = true;
    s_bus.sub_count++;

    ESP_LOGI(TAG, "Subscribed to type=%d, total=%d", type, s_bus.sub_count);
    return sub;
}

esp_err_t msg_bus_unsubscribe(msg_bus_sub_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->active = false;
    ESP_LOGI(TAG, "Unsubscribed type=%d", handle->type);
    return ESP_OK;
}

int msg_bus_pending_count(void)
{
    if (!s_bus.initialized) return 0;
    return (int)uxQueueMessagesWaiting(s_bus.queue);
}
