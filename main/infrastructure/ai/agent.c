#include <string.h>
#include <stdlib.h>
#include "agent.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"

static const char *TAG = "agent";

#define AGENT_MAX_TOOL_ROUNDS 10

static struct {
    char *api_key;
    char *model;
    char *system_prompt;
    float temperature;
    int max_tokens;
    bool thinking_enabled;
    cJSON *history;
    bool initialized;
} s_agent = {
    .temperature = 1.0f,
    .max_tokens = 4096,
    .thinking_enabled = false,
    .initialized = false,
};

typedef struct {
    char *buf;
    int len;
    int capacity;
} response_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    response_buf_t *resp = (response_buf_t *)evt->user_data;
    if (resp == NULL) {
        return ESP_OK;
    }

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (resp->len + evt->data_len < resp->capacity - 1) {
            memcpy(resp->buf + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
            resp->buf[resp->len] = '\0';
        } else {
            ESP_LOGW(TAG, "Response buffer overflow, truncating");
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static const char *role_to_str(agent_role_t role) __attribute__((unused));
static const char *role_to_str(agent_role_t role)
{
    switch (role) {
    case AGENT_ROLE_SYSTEM:    return "system";
    case AGENT_ROLE_USER:      return "user";
    case AGENT_ROLE_ASSISTANT: return "assistant";
    case AGENT_ROLE_TOOL:      return "tool";
    default:                   return "user";
    }
}

static void history_trim(void)
{
    while (cJSON_GetArraySize(s_agent.history) >= AGENT_MSG_HISTORY_MAX) {
        if (cJSON_GetArraySize(s_agent.history) <= 1) break;

        cJSON *item = cJSON_GetArrayItem(s_agent.history, 1);
        cJSON *role_j = item ? cJSON_GetObjectItem(item, "role") : NULL;
        const char *role = cJSON_IsString(role_j) ? role_j->valuestring : "";
        cJSON *tc = cJSON_GetObjectItem(item, "tool_calls");
        bool has_tool_calls = (strcmp(role, "assistant") == 0 &&
                               cJSON_IsArray(tc) && cJSON_GetArraySize(tc) > 0);

        cJSON_DeleteItemFromArray(s_agent.history, 1);

        if (has_tool_calls) {
            while (cJSON_GetArraySize(s_agent.history) > 1) {
                cJSON *next = cJSON_GetArrayItem(s_agent.history, 1);
                cJSON *nr = next ? cJSON_GetObjectItem(next, "role") : NULL;
                if (cJSON_IsString(nr) && strcmp(nr->valuestring, "tool") == 0) {
                    cJSON_DeleteItemFromArray(s_agent.history, 1);
                } else {
                    break;
                }
            }
        }
    }
}

static void history_add_message(const char *role, const char *content)
{
    if (s_agent.history == NULL) {
        return;
    }
    history_trim();

    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", role);
    cJSON_AddStringToObject(msg, "content", content);
    cJSON_AddItemToArray(s_agent.history, msg);
}

static void history_add_raw(cJSON *msg)
{
    if (s_agent.history == NULL || msg == NULL) {
        return;
    }
    history_trim();
    cJSON_AddItemToArray(s_agent.history, cJSON_Duplicate(msg, true));
}

esp_err_t agent_init(const agent_config_t *config)
{
    if (config == NULL || config->api_key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    free(s_agent.api_key);
    free(s_agent.model);
    free(s_agent.system_prompt);
    if (s_agent.history) {
        cJSON_Delete(s_agent.history);
    }

    s_agent.api_key = strdup(config->api_key);
    s_agent.model = strdup(config->model ? config->model : DEEPSEEK_MODEL_CHAT);
    s_agent.system_prompt = config->system_prompt ? strdup(config->system_prompt) : NULL;
    s_agent.temperature = config->temperature > 0 ? config->temperature : 1.0f;
    s_agent.max_tokens = config->max_tokens > 0 ? config->max_tokens : 4096;
    s_agent.thinking_enabled = config->thinking_enabled;

    s_agent.history = cJSON_CreateArray();

    if (s_agent.system_prompt) {
        history_add_message("system", s_agent.system_prompt);
    }

    s_agent.initialized = true;
    ESP_LOGI(TAG, "Agent initialized, model=%s, thinking=%s",
             s_agent.model, s_agent.thinking_enabled ? "on" : "off");
    return ESP_OK;
}

static void history_sanitize(void)
{
    if (!s_agent.history) return;
    int size = cJSON_GetArraySize(s_agent.history);

    for (int i = 1; i < size;) {
        cJSON *msg = cJSON_GetArrayItem(s_agent.history, i);
        cJSON *role_j = msg ? cJSON_GetObjectItem(msg, "role") : NULL;
        if (!cJSON_IsString(role_j)) { i++; continue; }

        if (strcmp(role_j->valuestring, "tool") == 0) {
            bool found_tc = false;
            for (int j = i - 1; j >= 1; j--) {
                cJSON *prev = cJSON_GetArrayItem(s_agent.history, j);
                cJSON *pr = prev ? cJSON_GetObjectItem(prev, "role") : NULL;
                if (!cJSON_IsString(pr)) break;
                if (strcmp(pr->valuestring, "tool") == 0) continue;
                if (strcmp(pr->valuestring, "assistant") == 0) {
                    cJSON *tc = cJSON_GetObjectItem(prev, "tool_calls");
                    found_tc = cJSON_IsArray(tc) && cJSON_GetArraySize(tc) > 0;
                }
                break;
            }
            if (!found_tc) {
                ESP_LOGW(TAG, "Removing orphaned tool message at index %d", i);
                cJSON_DeleteItemFromArray(s_agent.history, i);
                size--;
                continue;
            }
        }
        i++;
    }
}

static cJSON *build_request_body(const cJSON *messages, const cJSON *tools)
{
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", s_agent.model);
    cJSON_AddItemToObject(body, "messages", cJSON_Duplicate(messages, true));
    cJSON_AddBoolToObject(body, "stream", false);

    if (s_agent.thinking_enabled) {
        cJSON *thinking = cJSON_CreateObject();
        cJSON_AddStringToObject(thinking, "type", "enabled");
        cJSON_AddItemToObject(body, "thinking", thinking);
    } else {
        cJSON_AddNumberToObject(body, "temperature", s_agent.temperature);
    }

    cJSON_AddNumberToObject(body, "max_tokens", s_agent.max_tokens);

    if (tools && cJSON_GetArraySize(tools) > 0) {
        cJSON_AddItemToObject(body, "tools", cJSON_Duplicate(tools, true));
        cJSON_AddStringToObject(body, "tool_choice", "auto");
    }

    return body;
}

static cJSON *do_http_request(const char *body_str)
{
    response_buf_t resp = {
        .buf = heap_caps_calloc(1, AGENT_RESPONSE_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .len = 0,
        .capacity = AGENT_RESPONSE_BUF_SIZE,
    };
    if (resp.buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return NULL;
    }

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_agent.api_key);

    esp_http_client_config_t http_config = {
        .url = DEEPSEEK_API_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 30000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        free(resp.buf);
        return NULL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, body_str, strlen(body_str));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        free(resp.buf);
        return NULL;
    }

    ESP_LOGI(TAG, "HTTP status=%d, response_len=%d", status, resp.len);

    if (status != 200) {
        ESP_LOGE(TAG, "API error (status %d): %.*s", status, resp.len, resp.buf);
        free(resp.buf);
        return NULL;
    }

    cJSON *json = cJSON_Parse(resp.buf);
    free(resp.buf);

    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse API response JSON");
    }
    return json;
}

cJSON *agent_raw_request(const cJSON *messages_json, const cJSON *tools_json)
{
    if (!s_agent.initialized) {
        ESP_LOGE(TAG, "Agent not initialized");
        return NULL;
    }

    cJSON *body = build_request_body(messages_json, tools_json);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    if (body_str == NULL) {
        return NULL;
    }

    ESP_LOGI(TAG, "Sending request to DeepSeek API...");
    cJSON *response = do_http_request(body_str);
    free(body_str);

    return response;
}

static cJSON *extract_choice(const cJSON *response)
{
    cJSON *choices = cJSON_GetObjectItem(response, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        return NULL;
    }
    return cJSON_GetArrayItem(choices, 0);
}

typedef struct {
    bool executed;
    bool malformed;
} tool_call_exec_result_t;

static tool_call_exec_result_t execute_single_tool_call(const cJSON *tc, int round, const char **fail_reason)
{
    tool_call_exec_result_t ret = {
        .executed = false,
        .malformed = false,
    };

    cJSON *func = cJSON_GetObjectItem(tc, "function");
    cJSON *tc_id = cJSON_GetObjectItem(tc, "id");
    cJSON *func_name_j = func ? cJSON_GetObjectItem(func, "name") : NULL;
    cJSON *args_j = func ? cJSON_GetObjectItem(func, "arguments") : NULL;
    if (!cJSON_IsString(tc_id) || !cJSON_IsObject(func) || !cJSON_IsString(func_name_j) || !cJSON_IsString(args_j)) {
        ESP_LOGW(TAG, "Skipping malformed tool call in round %d", round + 1);
        ret.malformed = true;
        return ret;
    }

    const char *func_name = func_name_j->valuestring;
    const char *args_str = args_j->valuestring;

    ESP_LOGI(TAG, "Tool call: %s(%s)", func_name, args_str);

    cJSON *args = cJSON_Parse(args_str);
    cJSON *result = tool_registry_call(func_name, args);
    char *result_str = cJSON_PrintUnformatted(result);
    if (result_str == NULL) {
        cJSON_Delete(result);
        cJSON_Delete(args);
        ESP_LOGE(TAG, "Failed to serialize tool result for %s", func_name);
        if (fail_reason) {
            *fail_reason = "failed to serialize tool result";
        }
        ret.executed = true;
        return ret;
    }

    cJSON *tool_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_msg, "role", "tool");
    cJSON_AddStringToObject(tool_msg, "tool_call_id", tc_id->valuestring);
    cJSON_AddStringToObject(tool_msg, "content", result_str);
    history_add_raw(tool_msg);

    cJSON_Delete(tool_msg);
    free(result_str);
    cJSON_Delete(result);
    cJSON_Delete(args);
    ret.executed = true;
    return ret;
}

static esp_err_t process_tool_calls(const cJSON *tool_calls, int round, const char **fail_reason)
{
    int tool_call_count = cJSON_GetArraySize(tool_calls);
    int executed_tool_calls = 0;
    int malformed_tool_calls = 0;

    ESP_LOGI(TAG, "Processing %d tool call(s) (round %d)...", tool_call_count, round + 1);

    cJSON *tc = NULL;
    cJSON_ArrayForEach(tc, tool_calls) {
        tool_call_exec_result_t one = execute_single_tool_call(tc, round, fail_reason);
        if (one.malformed) {
            malformed_tool_calls++;
            continue;
        }
        if (one.executed) {
            if (fail_reason && *fail_reason && strcmp(*fail_reason, "failed to serialize tool result") == 0) {
                return ESP_FAIL;
            }
            executed_tool_calls++;
        }
    }

    if (executed_tool_calls == 0) {
        if (fail_reason) {
            if (malformed_tool_calls > 0) {
                *fail_reason = "all tool calls malformed";
            } else {
                *fail_reason = "no executable tool calls";
            }
        }
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void clear_reasoning_in_history(void)
{
    if (!s_agent.history) return;

    cJSON *msg = NULL;
    cJSON_ArrayForEach(msg, s_agent.history) {
        cJSON *rc = cJSON_GetObjectItem(msg, "reasoning_content");
        if (rc) {
            cJSON_ReplaceItemInObject(msg, "reasoning_content", cJSON_CreateNull());
        }
    }
}

static esp_err_t agent_chat_internal(const char *user_msg, char **out_content, char **out_reasoning)
{
    if (!s_agent.initialized || user_msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *committed_history = s_agent.history;
    cJSON *working_history = committed_history ? cJSON_Duplicate(committed_history, true) : cJSON_CreateArray();
    if (working_history == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const char *fail_reason = "unknown";
    cJSON *tools = NULL;

    s_agent.history = working_history;

    if (s_agent.thinking_enabled) {
        clear_reasoning_in_history();
    }

    history_add_message("user", user_msg);

    tools = tool_registry_get_schema();
    if (tools == NULL) {
        fail_reason = "tool schema allocation failed";
        goto fail;
    }

    for (int round = 0; round < AGENT_MAX_TOOL_ROUNDS; round++) {
        history_sanitize();

        cJSON *api_resp = agent_raw_request(s_agent.history, tools);
        if (api_resp == NULL) {
            fail_reason = "agent_raw_request returned NULL";
            goto fail;
        }

        cJSON *choice = extract_choice(api_resp);
        if (choice == NULL) {
            fail_reason = "response missing choices[0]";
            cJSON_Delete(api_resp);
            goto fail;
        }

        cJSON *message = cJSON_GetObjectItem(choice, "message");
        cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");

        if (!cJSON_IsObject(message)) {
            cJSON_Delete(api_resp);
            ESP_LOGE(TAG, "API response missing assistant message object");
            fail_reason = "response choice missing message object";
            goto fail;
        }

        history_add_raw(message);

        cJSON *reasoning = cJSON_GetObjectItem(message, "reasoning_content");
        if (cJSON_IsString(reasoning) && reasoning->valuestring && reasoning->valuestring[0]) {
            ESP_LOGI(TAG, "Thinking: %.100s...", reasoning->valuestring);
        }

        cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
        if (cJSON_IsArray(tool_calls) && cJSON_GetArraySize(tool_calls) > 0) {
            if (process_tool_calls(tool_calls, round, &fail_reason) != ESP_OK) {
                cJSON_Delete(api_resp);
                goto fail;
            }

            cJSON_Delete(api_resp);
            continue;
        }

        cJSON *content = cJSON_GetObjectItem(message, "content");
        if (out_content) {
            if (cJSON_IsString(content) && content->valuestring) {
                *out_content = strdup(content->valuestring);
            } else {
                *out_content = strdup("(empty response)");
            }
        }

        if (out_reasoning) {
            if (cJSON_IsString(reasoning) && reasoning->valuestring && reasoning->valuestring[0]) {
                *out_reasoning = strdup(reasoning->valuestring);
            } else {
                *out_reasoning = NULL;
            }
        }

        ESP_LOGI(TAG, "Chat completed, finish_reason=%s", cJSON_IsString(finish_reason) ? finish_reason->valuestring : "unknown");

        cJSON_Delete(api_resp);
        cJSON_Delete(tools);
        cJSON_Delete(committed_history);
        s_agent.history = working_history;
        return ESP_OK;
    }

    fail_reason = "max tool-call rounds exceeded";

fail:
    cJSON_Delete(tools);
    cJSON_Delete(working_history);
    s_agent.history = committed_history;
    ESP_LOGW(TAG, "Chat round failed (%s), discarded uncommitted history", fail_reason);
    return ESP_FAIL;
}

esp_err_t agent_chat(const char *user_msg, char *response, size_t response_size)
{
    if (response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *content = NULL;
    esp_err_t ret = agent_chat_internal(user_msg, &content, NULL);

    if (ret == ESP_OK && content) {
        strncpy(response, content, response_size - 1);
        response[response_size - 1] = '\0';
    } else if (ret != ESP_OK) {
        snprintf(response, response_size, "Error: chat failed");
    }

    free(content);
    return ret;
}

esp_err_t agent_chat_ex(const char *user_msg, agent_chat_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    return agent_chat_internal(user_msg, &result->content, &result->reasoning_content);
}

void agent_chat_result_free(agent_chat_result_t *result)
{
    if (result) {
        free(result->content);
        free(result->reasoning_content);
        result->content = NULL;
        result->reasoning_content = NULL;
    }
}

void agent_clear_history(void)
{
    if (s_agent.history) {
        cJSON_Delete(s_agent.history);
        s_agent.history = cJSON_CreateArray();

        if (s_agent.system_prompt) {
            history_add_message("system", s_agent.system_prompt);
        }
    }
    ESP_LOGI(TAG, "History cleared");
}

int agent_get_history_count(void)
{
    if (!s_agent.history) return 0;
    return cJSON_GetArraySize(s_agent.history);
}

void agent_set_thinking(bool enabled)
{
    s_agent.thinking_enabled = enabled;
    ESP_LOGI(TAG, "Thinking mode %s", enabled ? "enabled" : "disabled");
}

esp_err_t agent_update_system_prompt(const char *system_prompt)
{
    if (!s_agent.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (system_prompt == NULL || system_prompt[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char *new_prompt = strdup(system_prompt);
    if (new_prompt == NULL) {
        return ESP_ERR_NO_MEM;
    }

    free(s_agent.system_prompt);
    s_agent.system_prompt = new_prompt;

    if (s_agent.history == NULL) {
        s_agent.history = cJSON_CreateArray();
        if (s_agent.history == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    cJSON *first = cJSON_GetArrayItem(s_agent.history, 0);
    cJSON *role = first ? cJSON_GetObjectItem(first, "role") : NULL;
    if (cJSON_IsObject(first) && cJSON_IsString(role) && strcmp(role->valuestring, "system") == 0) {
        cJSON_ReplaceItemInObject(first, "content", cJSON_CreateString(s_agent.system_prompt));
    } else {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", "system");
        cJSON_AddStringToObject(msg, "content", s_agent.system_prompt);
        cJSON_InsertItemInArray(s_agent.history, 0, msg);
    }

    ESP_LOGI(TAG, "System prompt updated at runtime");
    return ESP_OK;
}