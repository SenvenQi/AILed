#include "tool_registry.h"
#include "led_strip_tool.h"
#include "esp_log.h"
#include <ctype.h>
#include <string.h>
#include "animation_downloader.h"
#include "animation_player.h"

static const char *TAG = "tool_builtins";

#define MATRIX_W 10
#define MATRIX_H 10
#define MATRIX_PIXELS (MATRIX_W * MATRIX_H)

static cJSON *handle_turn_on(const cJSON *params)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *r_j = cJSON_GetObjectItem(params, "r");
    cJSON *g_j = cJSON_GetObjectItem(params, "g");
    cJSON *b_j = cJSON_GetObjectItem(params, "b");

    uint8_t r = r_j ? (uint8_t)r_j->valueint : 255;
    uint8_t g = g_j ? (uint8_t)g_j->valueint : 180;
    uint8_t b = b_j ? (uint8_t)b_j->valueint : 50;

    esp_err_t err = led_strip_tool_fill(r, g, b);
    if (err == ESP_OK) {
        cJSON_AddStringToObject(result, "status", "ok");
        ESP_LOGI(TAG, "turn_on: r=%d g=%d b=%d, count=%d", r, g, b, led_strip_tool_get_count());
    } else {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", esp_err_to_name(err));
    }
    return result;
}

static cJSON *handle_turn_off(const cJSON *params)
{
    (void)params;
    cJSON *result = cJSON_CreateObject();
    esp_err_t err = led_strip_tool_clear();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(result, "status", "ok");
        ESP_LOGI(TAG, "turn_off: done");
    } else {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", esp_err_to_name(err));
    }
    return result;
}

static cJSON *handle_set_brightness(const cJSON *params)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *val = cJSON_GetObjectItem(params, "brightness");
    int brightness = val ? val->valueint : 100;

    led_strip_tool_set_brightness(brightness);
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "brightness", brightness);
    return result;
}

static cJSON *handle_set_led_color(const cJSON *params)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *index_j = cJSON_GetObjectItem(params, "index");
    cJSON *r_j = cJSON_GetObjectItem(params, "r");
    cJSON *g_j = cJSON_GetObjectItem(params, "g");
    cJSON *b_j = cJSON_GetObjectItem(params, "b");

    int index = index_j ? index_j->valueint : 0;
    uint8_t r = r_j ? (uint8_t)r_j->valueint : 0;
    uint8_t g = g_j ? (uint8_t)g_j->valueint : 0;
    uint8_t b = b_j ? (uint8_t)b_j->valueint : 0;

    led_strip_tool_stop_animation();

    esp_err_t err = led_strip_tool_set_pixel(index, r, g, b);
    if (err == ESP_OK) {
        err = led_strip_tool_refresh();
    }

    if (err == ESP_OK) {
        cJSON_AddStringToObject(result, "status", "ok");
    } else {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", esp_err_to_name(err));
    }
    return result;
}

static cJSON *handle_set_led_range(const cJSON *params)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *start_j = cJSON_GetObjectItem(params, "start");
    cJSON *end_j = cJSON_GetObjectItem(params, "end");
    cJSON *r_j = cJSON_GetObjectItem(params, "r");
    cJSON *g_j = cJSON_GetObjectItem(params, "g");
    cJSON *b_j = cJSON_GetObjectItem(params, "b");

    int start = start_j ? start_j->valueint : 0;
    int end = end_j ? end_j->valueint : 0;
    uint8_t r = r_j ? (uint8_t)r_j->valueint : 0;
    uint8_t g = g_j ? (uint8_t)g_j->valueint : 0;
    uint8_t b = b_j ? (uint8_t)b_j->valueint : 0;

    led_strip_tool_stop_animation();

    esp_err_t err = ESP_OK;
    for (int i = start; i <= end && err == ESP_OK; i++) {
        err = led_strip_tool_set_pixel(i, r, g, b);
    }
    if (err == ESP_OK) {
        err = led_strip_tool_refresh();
    }

    if (err == ESP_OK) {
        cJSON_AddStringToObject(result, "status", "ok");
        ESP_LOGI(TAG, "set_led_range: [%d-%d] r=%d g=%d b=%d", start, end, r, g, b);
    } else {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", esp_err_to_name(err));
    }
    return result;
}


static int matrix_xy_to_index(int x, int y, bool serpentine)
{
    if (!serpentine || (y % 2 == 0)) {
        return y * MATRIX_W + x;
    }
    return y * MATRIX_W + (MATRIX_W - 1 - x);
}

static cJSON *handle_draw_pattern_10x10(const cJSON *params)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *pattern_j = cJSON_GetObjectItem(params, "pattern");
    cJSON *on_r_j = cJSON_GetObjectItem(params, "on_r");
    cJSON *on_g_j = cJSON_GetObjectItem(params, "on_g");
    cJSON *on_b_j = cJSON_GetObjectItem(params, "on_b");
    cJSON *off_r_j = cJSON_GetObjectItem(params, "off_r");
    cJSON *off_g_j = cJSON_GetObjectItem(params, "off_g");
    cJSON *off_b_j = cJSON_GetObjectItem(params, "off_b");
    cJSON *serpentine_j = cJSON_GetObjectItem(params, "serpentine");

    if (!cJSON_IsString(pattern_j) || !pattern_j->valuestring) {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "pattern must be a string");
        return result;
    }

    uint8_t on_r = on_r_j ? (uint8_t)on_r_j->valueint : 255;
    uint8_t on_g = on_g_j ? (uint8_t)on_g_j->valueint : 255;
    uint8_t on_b = on_b_j ? (uint8_t)on_b_j->valueint : 255;
    uint8_t off_r = off_r_j ? (uint8_t)off_r_j->valueint : 0;
    uint8_t off_g = off_g_j ? (uint8_t)off_g_j->valueint : 0;
    uint8_t off_b = off_b_j ? (uint8_t)off_b_j->valueint : 0;
    bool serpentine = cJSON_IsBool(serpentine_j) ? cJSON_IsTrue(serpentine_j) : false;

    led_strip_tool_stop_animation();

    const char *pattern = pattern_j->valuestring;
    int bit_count = 0;

    for (int i = 0; pattern[i] != '\0'; i++) {
        char ch = pattern[i];
        if (isspace((unsigned char)ch) || ch == ',' || ch == '|' || ch == ';') {
            continue;
        }

        bool is_on = (ch == '1' || ch == '#' || ch == '*' || ch == 'X' || ch == 'x' || ch == '@');
        bool is_off = (ch == '0' || ch == '.' || ch == '-' || ch == '_');
        if (!is_on && !is_off) {
            continue;
        }

        if (bit_count >= MATRIX_PIXELS) {
            cJSON_AddStringToObject(result, "status", "error");
            cJSON_AddStringToObject(result, "message", "pattern has more than 100 pixels");
            return result;
        }

        int x = bit_count % MATRIX_W;
        int y = bit_count / MATRIX_W;
        int led_index = matrix_xy_to_index(x, y, serpentine);

        esp_err_t set_err = led_strip_tool_set_pixel(led_index,
                                                     is_on ? on_r : off_r,
                                                     is_on ? on_g : off_g,
                                                     is_on ? on_b : off_b);
        if (set_err != ESP_OK) {
            cJSON_AddStringToObject(result, "status", "error");
            cJSON_AddStringToObject(result, "message", esp_err_to_name(set_err));
            return result;
        }

        bit_count++;
    }

    if (bit_count != MATRIX_PIXELS) {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "pattern must provide exactly 100 pixels");
        cJSON_AddNumberToObject(result, "parsed_pixels", bit_count);
        return result;
    }

    esp_err_t err = led_strip_tool_refresh();
    if (err != ESP_OK) {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", esp_err_to_name(err));
        return result;
    }

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "width", MATRIX_W);
    cJSON_AddNumberToObject(result, "height", MATRIX_H);
    cJSON_AddBoolToObject(result, "serpentine", serpentine);
    return result;
}

// 通过关键词下载并播放动画
static cJSON *handle_play_animation_by_keyword(const cJSON *params)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *keyword_j = cJSON_GetObjectItem(params, "keyword");
    if (!cJSON_IsString(keyword_j) || strlen(keyword_j->valuestring) == 0) {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "keyword required");
        return result;
    }
    char local_path[128] = {0};
    bool ok = animation_downloader_download(keyword_j->valuestring, local_path, sizeof(local_path));
    if (!ok) {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "download failed");
        return result;
    }
    ok = animation_player_play(local_path);
    if (!ok) {
        cJSON_AddStringToObject(result, "status", "error");
        cJSON_AddStringToObject(result, "message", "play failed");
        return result;
    }
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddStringToObject(result, "message", "animation playing");
    cJSON_AddStringToObject(result, "keyword", keyword_j->valuestring);
    return result;
}


// 工具描述表（可扩展）
typedef struct {
    const tool_def_t *def;
} builtin_tool_entry_t;

void tool_registry_register_builtins(void)
{
    // 静态定义所有内置工具
tool_def_t builtin_tools[] = {
        {
            .name = "turn_on",
            .description = "Turn on all LEDs with specified RGB color. Default is warm white (255,180,50). Use this for commands like 'turn on the light' or 'light up all LEDs'.",
            .param_count = 3,
            .params = {
                { .name = "r", .description = "Red (0-255, default 255)", .type = TOOL_PARAM_TYPE_INT, .required = false, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "g", .description = "Green (0-255, default 180)", .type = TOOL_PARAM_TYPE_INT, .required = false, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "b", .description = "Blue (0-255, default 50)", .type = TOOL_PARAM_TYPE_INT, .required = false, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
            },
            .handler = handle_turn_on,
        },
        {
            .name = "turn_off",
            .description = "Turn off all LEDs and stop any running animation.",
            .param_count = 0,
            .handler = handle_turn_off,
        },
        {
            .name = "set_brightness",
            .description = "Set LED brightness level (0-100%). Affects all subsequent LED operations.",
            .param_count = 1,
            .params = {
                { .name = "brightness", .description = "Brightness percentage (0=off, 100=max)", .type = TOOL_PARAM_TYPE_INT, .required = true, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 100 },
            },
            .handler = handle_set_brightness,
        },
        {
            .name = "set_led_color",
            .description = "Set a single LED color by index. Only use for individual LED control.",
            .param_count = 4,
            .params = {
                { .name = "index", .description = "LED index (0-99)", .type = TOOL_PARAM_TYPE_INT, .required = true, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 99 },
                { .name = "r", .description = "Red (0-255)", .type = TOOL_PARAM_TYPE_INT, .required = true, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "g", .description = "Green (0-255)", .type = TOOL_PARAM_TYPE_INT, .required = true, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "b", .description = "Blue (0-255)", .type = TOOL_PARAM_TYPE_INT, .required = true, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
            },
            .handler = handle_set_led_color,
        },
        {
            .name = "set_led_range",
            .description = "Set a contiguous range of LEDs to the same color. Use this for lighting up sections. Total 100 LEDs (0-99).",
            .param_count = 5,
            .params = {
                { .name = "start", .description = "Start index (inclusive)", .type = TOOL_PARAM_TYPE_INT, .required = true, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 99 },
                { .name = "end", .description = "End index (inclusive)", .type = TOOL_PARAM_TYPE_INT, .required = true, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 99 },
                { .name = "r", .description = "Red (0-255)", .type = TOOL_PARAM_TYPE_INT, .required = true, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "g", .description = "Green (0-255)", .type = TOOL_PARAM_TYPE_INT, .required = true, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "b", .description = "Blue (0-255)", .type = TOOL_PARAM_TYPE_INT, .required = true, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
            },
            .handler = handle_set_led_range,
        },
        {
            .name = "draw_pattern_10x10",
            .description = "Draw a 10x10 LED matrix pattern. Default mapping is row-major (1-10 first row, 11 under 1). The pattern must contain exactly 100 pixels using on/off symbols. ON symbols: 1 # * X x @, OFF symbols: 0 . - _. Spaces/newlines/commas are ignored.",
            .param_count = 8,
            .params = {
                { .name = "pattern", .description = "10x10 pattern string (100 pixels). Example rows can use 1/0 or #/.", .type = TOOL_PARAM_TYPE_STRING, .required = true },
                { .name = "on_r", .description = "ON pixel red (0-255, default 255)", .type = TOOL_PARAM_TYPE_INT, .required = false, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "on_g", .description = "ON pixel green (0-255, default 255)", .type = TOOL_PARAM_TYPE_INT, .required = false, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "on_b", .description = "ON pixel blue (0-255, default 255)", .type = TOOL_PARAM_TYPE_INT, .required = false, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "off_r", .description = "OFF pixel red (0-255, default 0)", .type = TOOL_PARAM_TYPE_INT, .required = false, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "off_g", .description = "OFF pixel green (0-255, default 0)", .type = TOOL_PARAM_TYPE_INT, .required = false, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "off_b", .description = "OFF pixel blue (0-255, default 0)", .type = TOOL_PARAM_TYPE_INT, .required = false, .has_minimum = true, .minimum = 0, .has_maximum = true, .maximum = 255 },
                { .name = "serpentine", .description = "If true, odd rows are reversed (snake wiring). Default false. Keep false for row-major layout: 1-10 first row and 11 under 1.", .type = TOOL_PARAM_TYPE_BOOL, .required = false },
            },
            .handler = handle_draw_pattern_10x10,
        },
        {
            .name = "play_animation_by_keyword",
            .description = "Play an animation by keyword. The keyword must match an existing animation file.",
            .param_count = 1,
            .params = {
                { .name = "keyword", .description = "Animation keyword", .type = TOOL_PARAM_TYPE_STRING, .required = true },
            },
            .handler = handle_play_animation_by_keyword,
        },
    };
    int tool_count = sizeof(builtin_tools) / sizeof(builtin_tools[0]);
    for (int i = 0; i < tool_count; ++i) {
        tool_registry_register(&builtin_tools[i]);
    }
    ESP_LOGI(TAG, "Builtin tools registered, total=%d", tool_registry_count());
}
