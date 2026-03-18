#include "led_strip_tool.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <math.h>
#include <stdlib.h>

static const char *TAG = "led_strip_tool";

static led_strip_handle_t s_led_strip = NULL;
static int s_max_leds = 0;
static int s_brightness = 100;
static SemaphoreHandle_t s_led_mutex = NULL;
static TaskHandle_t s_anim_task = NULL;
static volatile led_anim_type_t s_anim_type = LED_ANIM_NONE;
static volatile bool s_anim_running = false;
static uint8_t s_anim_r = 255, s_anim_g = 255, s_anim_b = 255;
static int s_anim_speed = 50;

static bool led_strip_lock(TickType_t timeout_ticks)
{
    if (s_led_mutex == NULL) {
        return false;
    }
    return xSemaphoreTake(s_led_mutex, timeout_ticks) == pdTRUE;
}

static void led_strip_unlock(void)
{
    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}

static inline uint8_t scale_brightness(uint8_t val)
{
    return (uint8_t)((val * s_brightness) / 100);
}

static esp_err_t set_pixel_scaled(int index, uint8_t r, uint8_t g, uint8_t b)
{
    return led_strip_set_pixel(s_led_strip, index,
                               scale_brightness(r),
                               scale_brightness(g),
                               scale_brightness(b));
}

static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    h %= 360;
    uint8_t region = h / 60;
    uint8_t remainder = (h - (region * 60)) * 255 / 60;

    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
    case 0:  *r = v; *g = t; *b = p; break;
    case 1:  *r = q; *g = v; *b = p; break;
    case 2:  *r = p; *g = v; *b = t; break;
    case 3:  *r = p; *g = q; *b = v; break;
    case 4:  *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
    }
}

static inline uint8_t gamma_correct(uint8_t val)
{
    static const uint8_t gamma_lut[256] = {
          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,
          1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
          3,  3,  3,  3,  3,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,
          7,  7,  7,  8,  8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 12,
         13, 13, 14, 14, 15, 15, 16, 17, 17, 18, 18, 19, 19, 20, 21, 21,
         22, 23, 23, 24, 25, 25, 26, 27, 28, 28, 29, 30, 31, 31, 32, 33,
         34, 35, 36, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
         49, 50, 51, 52, 53, 54, 55, 57, 58, 59, 60, 61, 63, 64, 65, 66,
         68, 69, 70, 72, 73, 74, 76, 77, 79, 80, 81, 83, 84, 86, 87, 89,
         90, 92, 94, 95, 97, 98,100,102,103,105,107,108,110,112,114,115,
        117,119,121,123,124,126,128,130,132,134,136,138,140,142,144,146,
        148,150,152,154,156,158,160,163,165,167,169,171,174,176,178,180,
        183,185,187,190,192,194,197,199,201,204,206,209,211,214,216,219,
        221,224,226,229,231,234,237,239,242,245,247,250,253,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    };
    return gamma_lut[val];
}

static void anim_breathing(void)
{
    int step = 0;
    const int total_steps = 512;

    while (s_anim_running) {
        int raw = (step < 256) ? step : (511 - step);
        float factor = (float)gamma_correct((uint8_t)raw) / 255.0f;
        uint8_t r = (uint8_t)(s_anim_r * factor);
        uint8_t g = (uint8_t)(s_anim_g * factor);
        uint8_t b = (uint8_t)(s_anim_b * factor);

        if (led_strip_lock(pdMS_TO_TICKS(100))) {
            for (int i = 0; i < s_max_leds; i++) {
                set_pixel_scaled(i, r, g, b);
            }
            led_strip_refresh(s_led_strip);
            led_strip_unlock();
        }

        step = (step + 1) % total_steps;
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

static void anim_rainbow(void)
{
    int offset = 0;
    while (s_anim_running) {
        if (led_strip_lock(pdMS_TO_TICKS(100))) {
            for (int i = 0; i < s_max_leds; i++) {
                uint16_t hue = (i * 360 / s_max_leds + offset) % 360;
                uint8_t r, g, b;
                hsv_to_rgb(hue, 255, 200, &r, &g, &b);
                set_pixel_scaled(i, r, g, b);
            }
            led_strip_refresh(s_led_strip);
            led_strip_unlock();
        }

        offset = (offset + 3) % 360;
        vTaskDelay(pdMS_TO_TICKS(10 + (100 - s_anim_speed)));
    }
}

static void anim_chase(void)
{
    int pos = 0;
    int tail_len = s_max_leds > 20 ? 10 : s_max_leds / 3;
    while (s_anim_running) {
        if (led_strip_lock(pdMS_TO_TICKS(100))) {
            for (int i = 0; i < s_max_leds; i++) {
                int dist = (i - pos + s_max_leds) % s_max_leds;
                if (dist < tail_len) {
                    float factor = 1.0f - (float)dist / tail_len;
                    set_pixel_scaled(i,
                        (uint8_t)(s_anim_r * factor),
                        (uint8_t)(s_anim_g * factor),
                        (uint8_t)(s_anim_b * factor));
                } else {
                    set_pixel_scaled(i, 0, 0, 0);
                }
            }
            led_strip_refresh(s_led_strip);
            led_strip_unlock();
        }

        pos = (pos + 1) % s_max_leds;
        vTaskDelay(pdMS_TO_TICKS(20 + (100 - s_anim_speed) * 2));
    }
}

static void anim_twinkle(void)
{
    while (s_anim_running) {
        if (!led_strip_lock(pdMS_TO_TICKS(100))) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        for (int i = 0; i < s_max_leds; i++) {
            if ((rand() % 100) < 5) {
                set_pixel_scaled(i, s_anim_r, s_anim_g, s_anim_b);
            } else {
                if ((rand() % 100) < 15) {
                    set_pixel_scaled(i, 0, 0, 0);
                }
            }
        }
        led_strip_refresh(s_led_strip);
        led_strip_unlock();
        vTaskDelay(pdMS_TO_TICKS(30 + (100 - s_anim_speed) * 2));
    }
}

static void anim_gradient(void)
{
    int offset = 0;
    while (s_anim_running) {
        if (led_strip_lock(pdMS_TO_TICKS(100))) {
            for (int i = 0; i < s_max_leds; i++) {
                int pos = (i + offset) % s_max_leds;
                float factor = (float)pos / s_max_leds;
                set_pixel_scaled(i,
                    (uint8_t)(s_anim_r * factor),
                    (uint8_t)(s_anim_g * factor),
                    (uint8_t)(s_anim_b * factor));
            }
            led_strip_refresh(s_led_strip);
            led_strip_unlock();
        }

        offset = (offset + 1) % s_max_leds;
        vTaskDelay(pdMS_TO_TICKS(30 + (100 - s_anim_speed)));
    }
}

static void anim_fire(void)
{
    while (s_anim_running) {
        if (led_strip_lock(pdMS_TO_TICKS(100))) {
            for (int i = 0; i < s_max_leds; i++) {
                uint8_t flicker = rand() % 150;
                uint8_t r = 255 - flicker;
                uint8_t g = (80 > flicker) ? 80 - flicker : 0;
                set_pixel_scaled(i, r, g, 0);
            }
            led_strip_refresh(s_led_strip);
            led_strip_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(30 + (100 - s_anim_speed)));
    }
}

static void anim_rain(void)
{
    uint8_t *drops = calloc(s_max_leds, 1);
    if (!drops) return;

    while (s_anim_running) {
        if (!led_strip_lock(pdMS_TO_TICKS(100))) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if ((rand() % 100) < 30) {
            int pos = rand() % s_max_leds;
            drops[pos] = 200 + rand() % 56;
        }

        for (int i = 0; i < s_max_leds; i++) {
            if (drops[i] > 0) {
                uint8_t v = drops[i];
                set_pixel_scaled(i, v / 10, v / 4, v);
                drops[i] = (v > 20) ? v - 20 : 0;
            } else {
                set_pixel_scaled(i, 0, 0, 0);
            }
        }
        led_strip_refresh(s_led_strip);
        led_strip_unlock();
        vTaskDelay(pdMS_TO_TICKS(25 + (100 - s_anim_speed)));
    }
    free(drops);
}

static void anim_snow(void)
{
    uint8_t *flakes = calloc(s_max_leds, 1);
    if (!flakes) return;

    while (s_anim_running) {
        if (!led_strip_lock(pdMS_TO_TICKS(100))) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if ((rand() % 100) < 15) {
            int pos = rand() % s_max_leds;
            flakes[pos] = 180 + rand() % 76;
        }

        for (int i = 0; i < s_max_leds; i++) {
            if (flakes[i] > 0) {
                uint8_t v = flakes[i];
                set_pixel_scaled(i, v, v, v);
                flakes[i] = (v > 8) ? v - 8 : 0;
            } else {
                set_pixel_scaled(i, 0, 0, 0);
            }
        }
        led_strip_refresh(s_led_strip);
        led_strip_unlock();
        vTaskDelay(pdMS_TO_TICKS(40 + (100 - s_anim_speed)));
    }
    free(flakes);
}

static void anim_sunny(void)
{
    int step = 0;
    while (s_anim_running) {
        float phase = (float)step / 150.0f * 3.14159f;
        float wave = (sinf(phase) + 1.0f) / 2.0f * 0.3f + 0.7f;
        if (led_strip_lock(pdMS_TO_TICKS(100))) {
            for (int i = 0; i < s_max_leds; i++) {
                float local = wave + sinf((float)i / 8.0f + phase) * 0.1f;
                if (local > 1.0f) local = 1.0f;
                set_pixel_scaled(i,
                    (uint8_t)(255 * local),
                    (uint8_t)(200 * local),
                    (uint8_t)(60 * local));
            }
            led_strip_refresh(s_led_strip);
            led_strip_unlock();
        }
        step = (step + 1) % 300;
        vTaskDelay(pdMS_TO_TICKS(30 + (100 - s_anim_speed)));
    }
}

static void animation_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Animation task started, type=%d", s_anim_type);

    switch (s_anim_type) {
    case LED_ANIM_BREATHING: anim_breathing(); break;
    case LED_ANIM_RAINBOW:   anim_rainbow(); break;
    case LED_ANIM_CHASE:     anim_chase(); break;
    case LED_ANIM_TWINKLE:   anim_twinkle(); break;
    case LED_ANIM_GRADIENT:  anim_gradient(); break;
    case LED_ANIM_FIRE:      anim_fire(); break;
    case LED_ANIM_RAIN:      anim_rain(); break;
    case LED_ANIM_SNOW:      anim_snow(); break;
    case LED_ANIM_SUNNY:     anim_sunny(); break;
    default: break;
    }

    if (led_strip_lock(pdMS_TO_TICKS(100))) {
        led_strip_clear(s_led_strip);
        led_strip_unlock();
    }
    s_anim_type = LED_ANIM_NONE;
    s_anim_task = NULL;
    ESP_LOGI(TAG, "Animation task exited");
    vTaskDelete(NULL);
}

esp_err_t led_strip_tool_init(int gpio_num, int max_leds)
{
    if (s_led_mutex == NULL) {
        s_led_mutex = xSemaphoreCreateMutex();
        if (s_led_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_led_strip != NULL) {
        ESP_LOGW(TAG, "LED strip already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_num,
        .max_leds = max_leds,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
        return err;
    }

    s_max_leds = max_leds;
    if (led_strip_lock(pdMS_TO_TICKS(100))) {
        led_strip_clear(s_led_strip);
        led_strip_unlock();
    }
    ESP_LOGI(TAG, "LED strip initialized, gpio=%d, leds=%d", gpio_num, max_leds);
    return ESP_OK;
}

esp_err_t led_strip_tool_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led_strip == NULL) return ESP_ERR_INVALID_STATE;
    if (!led_strip_lock(pdMS_TO_TICKS(100))) return ESP_ERR_TIMEOUT;
    esp_err_t err = set_pixel_scaled(index, r, g, b);
    led_strip_unlock();
    return err;
}

esp_err_t led_strip_tool_refresh(void)
{
    if (s_led_strip == NULL) return ESP_ERR_INVALID_STATE;
    if (!led_strip_lock(pdMS_TO_TICKS(100))) return ESP_ERR_TIMEOUT;
    esp_err_t err = led_strip_refresh(s_led_strip);
    led_strip_unlock();
    return err;
}

esp_err_t led_strip_tool_clear(void)
{
    if (s_led_strip == NULL) return ESP_ERR_INVALID_STATE;
    led_strip_tool_stop_animation();
    if (!led_strip_lock(pdMS_TO_TICKS(100))) return ESP_ERR_TIMEOUT;
    esp_err_t err = led_strip_clear(s_led_strip);
    led_strip_unlock();
    return err;
}

int led_strip_tool_get_count(void)
{
    return s_max_leds;
}

esp_err_t led_strip_tool_fill(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led_strip == NULL) return ESP_ERR_INVALID_STATE;
    led_strip_tool_stop_animation();
    if (!led_strip_lock(pdMS_TO_TICKS(100))) return ESP_ERR_TIMEOUT;
    for (int i = 0; i < s_max_leds; i++) {
        set_pixel_scaled(i, r, g, b);
    }
    esp_err_t err = led_strip_refresh(s_led_strip);
    led_strip_unlock();
    return err;
}

esp_err_t led_strip_tool_set_brightness(int brightness)
{
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    s_brightness = brightness;
    ESP_LOGI(TAG, "Brightness set to %d%%", brightness);
    return ESP_OK;
}

int led_strip_tool_get_brightness(void)
{
    return s_brightness;
}

esp_err_t led_strip_tool_start_animation(led_anim_type_t type, uint8_t r, uint8_t g, uint8_t b, int speed)
{
    led_strip_tool_stop_animation();

    s_anim_r = r;
    s_anim_g = g;
    s_anim_b = b;
    s_anim_speed = speed < 1 ? 1 : (speed > 100 ? 100 : speed);
    s_anim_type = type;
    s_anim_running = true;

    BaseType_t ret = xTaskCreateWithCaps(animation_task, "led_anim", 4096, NULL,
                                          4, &s_anim_task, MALLOC_CAP_SPIRAM);
    if (ret != pdPASS) {
        s_anim_running = false;
        s_anim_type = LED_ANIM_NONE;
        ESP_LOGE(TAG, "Failed to create animation task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t led_strip_tool_stop_animation(void)
{
    if (!s_anim_running) return ESP_OK;
    s_anim_running = false;

    for (int i = 0; i < 50 && s_anim_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

bool led_strip_tool_is_animating(void)
{
    return s_anim_running;
}

led_strip_handle_t led_strip_tool_get_handle(void)
{
    return s_led_strip;
}
