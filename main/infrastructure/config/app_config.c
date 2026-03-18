#include "app_config.h"

#define APP_DEEPSEEK_API_KEY "sk-e7bc9198b38047b08d55cf4afe6d0ce5"

static const char *s_system_prompt =
    "你是一个智能 LED 灯带控制助手，运行在 ESP32 上。\n"
    "灯带共有 100 颗 LED（索引 0-99）。\n"
    "可用工具:\n"
    "- turn_on: 开灯(全部点亮，可指定颜色，默认暖白)\n"
    "- turn_off: 关灯\n"
    "- set_brightness: 调亮度(0-100%)\n"
    "- set_led_range: 批量设灯(连续区间同色)\n"
    "- set_led_color: 单灯设色\n"
    "- draw_pattern_10x10: 按10x10图案绘制(用1/0或#/.)\n"
    "- start_animation: 启动动画(breathing/rainbow/chase/twinkle/gradient/fire/rain/snow/sunny)\n"
    "- stop_animation: 停止动画\n"
    "请用简洁的中文回复。";

const char *app_system_prompt(void)
{
    return s_system_prompt;
}

agent_config_t app_make_agent_config(void)
{
    return (agent_config_t){
        .api_key = APP_DEEPSEEK_API_KEY,
        .model = DEEPSEEK_MODEL_CHAT,
        .system_prompt = app_system_prompt(),
        .temperature = 1.0f,
        .max_tokens = 4096,
        .thinking_enabled = false,
    };
}

config_mode_config_t app_make_config_mode_config(void)
{
    return (config_mode_config_t){
        .service_name_prefix = "AILed",
        .proof_of_possession = "ailed-ble",
        .wifi_connect_timeout_ms = 15000,
    };
}
