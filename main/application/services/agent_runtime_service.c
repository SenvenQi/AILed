#include "agent_runtime_service.h"

#include <stdlib.h>
#include <string.h>

#include "application/services/conversation_payload_mapper.h"
#include "cJSON.h"
#include "domain/agent/system_prompt_policy.h"
#include "domain/conversation/response_route_policy.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "agent_runtime_service";
#define AGENT_QUEUE_DEPTH 4
#define AGENT_TASK_STACK  (16 * 1024)
#define AGENT_TASK_PRIO   5

static QueueHandle_t s_agent_queue = NULL;
static agent_runtime_ports_t s_ports = {0};

static void publish_routed_ai_text(const char *text, const conversation_user_input_t *req)
{
    char *resp_str = conversation_build_ai_response_payload(text,
                                                            req ? req->chat_id : NULL,
                                                            req ? req->message_id : NULL);

    if (resp_str) {
        s_ports.publish(MSG_TYPE_AI_RESPONSE, resp_str, 1000);
        free(resp_str);
    }
}

static void on_user_input(const msg_bus_msg_t *msg, void *user_data)
{
    (void)user_data;
    if (!msg || !msg->data || !s_agent_queue) {
        return;
    }

    conversation_user_input_t req = {0};
    if (!conversation_parse_user_input(msg->data, &req)) {
        return;
    }

    if (!req.text || req.text[0] == '\0') {
        conversation_free_user_input(&req);
        return;
    }

    if (xQueueSend(s_agent_queue, &req, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Agent queue full, dropping message");
        conversation_free_user_input(&req);
    }
}

static void on_ai_response(const msg_bus_msg_t *msg, void *user_data)
{
    (void)user_data;
    if (!msg || !msg->data) {
        return;
    }

    const char *text = "";
    const char *chat_id = "";
    const char *message_id = "";
    void *json_root = NULL;

    if (!conversation_extract_ai_response_route(msg->data,
                                                &text,
                                                &chat_id,
                                                &message_id,
                                                &json_root)) {
        return;
    }

    conversation_route_type_t route = conversation_route_decide(chat_id, message_id);
    if (route == CONVERSATION_ROUTE_REPLY_MESSAGE) {
        s_ports.reply_message(message_id, text);
    } else if (route == CONVERSATION_ROUTE_SEND_CHAT) {
        s_ports.send_message(chat_id, text);
    } else {
        ESP_LOGW(TAG, "AI response has no routing info, discarding");
    }

    conversation_release_json_root(json_root);
}

static void on_system_message(const msg_bus_msg_t *msg, void *user_data)
{
    (void)user_data;
    if (!msg || !msg->data) {
        return;
    }

    cJSON *root = cJSON_Parse(msg->data);
    if (!root) {
        return;
    }

    cJSON *kind_j = cJSON_GetObjectItem(root, "kind");
    if (!cJSON_IsString(kind_j) || strcmp(kind_j->valuestring, "agent_system_context") != 0) {
        cJSON_Delete(root);
        return;
    }

    cJSON *soul_j = cJSON_GetObjectItem(root, "soul");
    cJSON *user_j = cJSON_GetObjectItem(root, "user");
    const char *soul = cJSON_IsString(soul_j) ? soul_j->valuestring : "";
    const char *user = cJSON_IsString(user_j) ? user_j->valuestring : "";

    char *merged = agent_system_prompt_merge_with_context(s_ports.base_system_prompt(), soul, user);
    if (!merged) {
        cJSON_Delete(root);
        return;
    }

    esp_err_t err = s_ports.agent_update_system_prompt(merged);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply system context: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Applied system context from message bus");
    }

    free(merged);
    cJSON_Delete(root);
}

static void agent_task(void *arg)
{
    (void)arg;
    conversation_user_input_t req;

    ESP_LOGI(TAG, "Agent task started");

    for (;;) {
        if (xQueueReceive(s_agent_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "Agent processing: %.60s%s", req.text,
                 strlen(req.text) > 60 ? "..." : "");

        agent_chat_result_t result = {0};
        esp_err_t err = s_ports.agent_chat_ex(req.text, &result);

        if (err == ESP_OK && result.content) {
            if (result.reasoning_content) {
                s_ports.publish(MSG_TYPE_AI_THINKING, result.reasoning_content, 1000);
            }
            publish_routed_ai_text(result.content, &req);
        } else {
            ESP_LOGE(TAG, "Agent chat failed: %s", esp_err_to_name(err));
            publish_routed_ai_text("抱歉，当前请求处理失败，请稍后重试。", &req);
        }

        s_ports.agent_chat_result_free(&result);
        conversation_free_user_input(&req);
    }
}

esp_err_t agent_runtime_service_start(const agent_runtime_ports_t *ports)
{
    if (!ports || !ports->init_agent || !ports->agent_chat_ex ||
        !ports->base_system_prompt || !ports->agent_chat_result_free ||
        !ports->agent_update_system_prompt ||
        !ports->publish || !ports->subscribe || !ports->send_message ||
        !ports->reply_message) {
        return ESP_ERR_INVALID_ARG;
    }

    s_ports = *ports;

    ESP_RETURN_ON_ERROR(s_ports.init_agent(), TAG, "Failed to init agent");

    s_agent_queue = xQueueCreate(AGENT_QUEUE_DEPTH, sizeof(conversation_user_input_t));
    if (!s_agent_queue) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_ok = xTaskCreateWithCaps(agent_task,
                                             "agent",
                                             AGENT_TASK_STACK,
                                             NULL,
                                             AGENT_TASK_PRIO,
                                             NULL,
                                             MALLOC_CAP_SPIRAM);
    if (task_ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    if (!s_ports.subscribe(MSG_TYPE_USER_INPUT, on_user_input, NULL)) {
        ESP_LOGW(TAG, "Failed to subscribe USER_INPUT");
    }
    if (!s_ports.subscribe(MSG_TYPE_AI_RESPONSE, on_ai_response, NULL)) {
        ESP_LOGW(TAG, "Failed to subscribe AI_RESPONSE");
    }
    if (!s_ports.subscribe(MSG_TYPE_SYSTEM, on_system_message, NULL)) {
        ESP_LOGW(TAG, "Failed to subscribe SYSTEM");
    }

    return ESP_OK;
}
