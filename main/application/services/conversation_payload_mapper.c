#include "conversation_payload_mapper.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

bool conversation_parse_user_input(const char *json, conversation_user_input_t *out)
{
    if (!json || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return false;
    }

    cJSON *text_j = cJSON_GetObjectItem(root, "text");
    cJSON *chat_id_j = cJSON_GetObjectItem(root, "chat_id");
    cJSON *msg_id_j = cJSON_GetObjectItem(root, "message_id");

    if (cJSON_IsString(text_j)) {
        out->text = strdup(text_j->valuestring);
    }
    if (cJSON_IsString(chat_id_j)) {
        out->chat_id = strdup(chat_id_j->valuestring);
    }
    if (cJSON_IsString(msg_id_j)) {
        out->message_id = strdup(msg_id_j->valuestring);
    }

    cJSON_Delete(root);
    return true;
}

void conversation_free_user_input(conversation_user_input_t *in)
{
    if (!in) {
        return;
    }

    free(in->text);
    free(in->chat_id);
    free(in->message_id);
    in->text = NULL;
    in->chat_id = NULL;
    in->message_id = NULL;
}

char *conversation_build_ai_response_payload(const char *text,
                                             const char *chat_id,
                                             const char *message_id)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return NULL;
    }

    cJSON_AddStringToObject(resp, "text", text ? text : "");
    if (chat_id) {
        cJSON_AddStringToObject(resp, "chat_id", chat_id);
    }
    if (message_id) {
        cJSON_AddStringToObject(resp, "message_id", message_id);
    }

    char *resp_str = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    return resp_str;
}

bool conversation_extract_ai_response_route(const char *json,
                                            const char **text,
                                            const char **chat_id,
                                            const char **message_id,
                                            void **json_root)
{
    if (!json || !text || !chat_id || !message_id || !json_root) {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return false;
    }

    cJSON *text_j = cJSON_GetObjectItem(root, "text");
    cJSON *chat_id_j = cJSON_GetObjectItem(root, "chat_id");
    cJSON *msg_id_j = cJSON_GetObjectItem(root, "message_id");

    *text = cJSON_IsString(text_j) ? text_j->valuestring : "";
    *chat_id = cJSON_IsString(chat_id_j) ? chat_id_j->valuestring : "";
    *message_id = cJSON_IsString(msg_id_j) ? msg_id_j->valuestring : "";
    *json_root = root;
    return true;
}

void conversation_release_json_root(void *json_root)
{
    cJSON_Delete((cJSON *)json_root);
}
