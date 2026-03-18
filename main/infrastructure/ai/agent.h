#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"
#include "tool_registry.h"

#define DEEPSEEK_API_URL        "https://api.deepseek.com/chat/completions"
#define DEEPSEEK_MODEL_CHAT     "deepseek-chat"
#define DEEPSEEK_MODEL_REASONER "deepseek-reasoner"
#define AGENT_MSG_HISTORY_MAX   20
#define AGENT_RESPONSE_BUF_SIZE (16 * 1024)

typedef enum {
    AGENT_ROLE_SYSTEM,
    AGENT_ROLE_USER,
    AGENT_ROLE_ASSISTANT,
    AGENT_ROLE_TOOL,
} agent_role_t;

typedef struct {
    const char *api_key;
    const char *model;
    const char *system_prompt;
    float temperature;
    int max_tokens;
    bool thinking_enabled;
} agent_config_t;

typedef struct {
    char *content;
    char *reasoning_content;
} agent_chat_result_t;

esp_err_t agent_init(const agent_config_t *config);
esp_err_t agent_chat(const char *user_msg, char *response, size_t response_size);
esp_err_t agent_chat_ex(const char *user_msg, agent_chat_result_t *result);
void agent_chat_result_free(agent_chat_result_t *result);
cJSON *agent_raw_request(const cJSON *messages_json, const cJSON *tools_json);
void agent_clear_history(void);
int agent_get_history_count(void);
void agent_set_thinking(bool enabled);
esp_err_t agent_update_system_prompt(const char *system_prompt);