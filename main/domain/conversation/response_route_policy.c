#include "response_route_policy.h"

conversation_route_type_t conversation_route_decide(const char *chat_id,
                                                    const char *message_id)
{
    if (message_id && message_id[0]) {
        return CONVERSATION_ROUTE_REPLY_MESSAGE;
    }
    if (chat_id && chat_id[0]) {
        return CONVERSATION_ROUTE_SEND_CHAT;
    }
    return CONVERSATION_ROUTE_NONE;
}
