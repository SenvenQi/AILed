#pragma once

#include "esp_err.h"

#define FEISHU_NVS_NAMESPACE     "feishu"
#define FEISHU_NVS_KEY_APP_ID    "app_id"
#define FEISHU_NVS_KEY_APP_SEC   "app_secret"
#define FEISHU_DEFAULT_APP_ID    "cli_a933bb1ada381cba"
#define FEISHU_DEFAULT_APP_SEC   "mGlvbtBEKEN4ajGqXMbMWb4l6NdaceaO"
#define FEISHU_MAX_MSG_LEN       4000
#define FEISHU_WS_STACK          (16 * 1024)
#define FEISHU_WS_INNER_STACK    (16 * 1024)
#define FEISHU_WS_PRIO           5
#define FEISHU_WS_CORE           1

esp_err_t feishu_bot_init(void);
esp_err_t feishu_bot_start(void);
esp_err_t feishu_send_message(const char *chat_id, const char *text);
esp_err_t feishu_reply_message(const char *message_id, const char *text);
esp_err_t feishu_set_credentials(const char *app_id, const char *app_secret);