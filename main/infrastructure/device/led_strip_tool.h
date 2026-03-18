#pragma once

#include "esp_err.h"
#include "led_strip.h"

typedef enum {
    LED_ANIM_NONE = 0,
    LED_ANIM_BREATHING,
    LED_ANIM_RAINBOW,
    LED_ANIM_CHASE,
    LED_ANIM_TWINKLE,
    LED_ANIM_GRADIENT,
    LED_ANIM_FIRE,
    LED_ANIM_RAIN,
    LED_ANIM_SNOW,
    LED_ANIM_SUNNY,
} led_anim_type_t;

esp_err_t led_strip_tool_init(int gpio_num, int max_leds);
esp_err_t led_strip_tool_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b);
esp_err_t led_strip_tool_refresh(void);
esp_err_t led_strip_tool_clear(void);
int led_strip_tool_get_count(void);
esp_err_t led_strip_tool_fill(uint8_t r, uint8_t g, uint8_t b);
esp_err_t led_strip_tool_set_brightness(int brightness);
int led_strip_tool_get_brightness(void);
esp_err_t led_strip_tool_start_animation(led_anim_type_t type, uint8_t r, uint8_t g, uint8_t b, int speed);
esp_err_t led_strip_tool_stop_animation(void);
bool led_strip_tool_is_animating(void);
led_strip_handle_t led_strip_tool_get_handle(void);
