#pragma once

typedef enum {
    CONVERSATION_ROUTE_NONE = 0,
    CONVERSATION_ROUTE_REPLY_MESSAGE,
    CONVERSATION_ROUTE_SEND_CHAT,
} conversation_route_type_t;

conversation_route_type_t conversation_route_decide(const char *chat_id,
                                                    const char *message_id);
