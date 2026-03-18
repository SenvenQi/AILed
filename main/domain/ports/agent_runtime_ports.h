#pragma once
#include <stdbool.h>
#include "agent.h"
#include "esp_err.h"
#include "message_bus.h"
typedef struct {
    esp_err_t (*init_agent)(void);
    const char *(*base_system_prompt)(void);
    esp_err_t (*agent_chat_ex)(const char *user_msg, agent_chat_result_t *result);
    void (*agent_chat_result_free)(agent_chat_result_t *result);
    esp_err_t (*agent_update_system_prompt)(const char *system_prompt);
    esp_err_t (*publish)(msg_type_t type, const char *data, uint32_t timeout_ms);
    bool (*subscribe)(msg_type_t type, msg_bus_handler_t handler, void *user_data);
    esp_err_t (*send_message)(const char *chat_id, const char *text);
    esp_err_t (*reply_message)(const char *message_id, const char *text);
} agent_runtime_ports_t;
