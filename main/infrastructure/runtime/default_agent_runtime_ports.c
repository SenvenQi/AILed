#include "default_agent_runtime_ports.h"
#include "domain/ports/agent_runtime_ports.h"
#include "agent.h"
#include "app_config.h"
#include "feishu_bot.h"
#include "message_bus.h"

static esp_err_t port_init_agent(void)
{
    agent_config_t cfg = app_make_agent_config();
    return agent_init(&cfg);
}

static const char *port_base_system_prompt(void)
{
    return app_system_prompt();
}

static esp_err_t port_agent_chat_ex(const char *user_msg, agent_chat_result_t *result)
{
    return agent_chat_ex(user_msg, result);
}

static void port_agent_chat_result_free(agent_chat_result_t *result)
{
    agent_chat_result_free(result);
}

static esp_err_t port_agent_update_system_prompt(const char *system_prompt)
{
    return agent_update_system_prompt(system_prompt);
}

static esp_err_t port_publish(msg_type_t type, const char *data, uint32_t timeout_ms)
{
    return msg_bus_publish(type, data, timeout_ms);
}

static bool port_subscribe(msg_type_t type, msg_bus_handler_t handler, void *user_data)
{
    return msg_bus_subscribe(type, handler, user_data) != NULL;
}

static esp_err_t port_send_message(const char *chat_id, const char *text)
{
    return feishu_send_message(chat_id, text);
}

static esp_err_t port_reply_message(const char *message_id, const char *text)
{
    return feishu_reply_message(message_id, text);
}

agent_runtime_ports_t default_agent_runtime_ports_build(void)
{
    agent_runtime_ports_t ports = {
        .init_agent = port_init_agent,
        .base_system_prompt = port_base_system_prompt,
        .agent_chat_ex = port_agent_chat_ex,
        .agent_chat_result_free = port_agent_chat_result_free,
        .agent_update_system_prompt = port_agent_update_system_prompt,
        .publish = port_publish,
        .subscribe = port_subscribe,
        .send_message = port_send_message,
        .reply_message = port_reply_message,
    };

    return ports;
}
