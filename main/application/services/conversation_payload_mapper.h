#pragma once

#include <stdbool.h>

typedef struct {
    char *text;
    char *chat_id;
    char *message_id;
} conversation_user_input_t;

bool conversation_parse_user_input(const char *json, conversation_user_input_t *out);
void conversation_free_user_input(conversation_user_input_t *in);

char *conversation_build_ai_response_payload(const char *text,
                                             const char *chat_id,
                                             const char *message_id);

bool conversation_extract_ai_response_route(const char *json,
                                            const char **text,
                                            const char **chat_id,
                                            const char **message_id,
                                            void **json_root);

void conversation_release_json_root(void *json_root);
