#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    MSG_TYPE_USER_INPUT,
    MSG_TYPE_AI_RESPONSE,
    MSG_TYPE_AI_THINKING,
    MSG_TYPE_TOOL_CALL,
    MSG_TYPE_TOOL_RESULT,
    MSG_TYPE_SYSTEM,
    MSG_TYPE_MAX,
} msg_type_t;

typedef struct {
    msg_type_t type;
    char *data;
    uint32_t data_len;
    int64_t timestamp;
} msg_bus_msg_t;

typedef void (*msg_bus_handler_t)(const msg_bus_msg_t *msg, void *user_data);

typedef struct msg_bus_sub *msg_bus_sub_handle_t;

esp_err_t msg_bus_init(int queue_size);
esp_err_t msg_bus_publish(msg_type_t type, const char *data, uint32_t timeout_ms);
msg_bus_sub_handle_t msg_bus_subscribe(msg_type_t type, msg_bus_handler_t handler, void *user_data);
esp_err_t msg_bus_unsubscribe(msg_bus_sub_handle_t handle);
int msg_bus_pending_count(void);
