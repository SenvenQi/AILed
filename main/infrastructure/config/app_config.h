#pragma once

#include "agent.h"
#include "config_mode.h"

#define APP_LED_GPIO          48
#define APP_LED_COUNT         100
#define APP_AGENT_TASK_STACK  (16 * 1024)
#define APP_AGENT_TASK_PRIO   5
#define APP_AGENT_QUEUE_DEPTH 4

const char *app_system_prompt(void);
agent_config_t app_make_agent_config(void);
config_mode_config_t app_make_config_mode_config(void);
