#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t time_sntp_init(const char *server);
esp_err_t time_sntp_stop(void);
bool time_is_synchronized(void);
esp_err_t time_wait_for_sync(uint32_t timeout_ms);

/* 秒级控制 API */
/**
 * @brief 等待直到下一个整秒到达（相对于系统时间）。
 * @param timeout_ms 最大等待时间（毫秒）。
 * @return ESP_OK 成功在超时时间内到达整秒，ESP_ERR_TIMEOUT 超时，ESP_ERR_INVALID_STATE 未初始化。
 */
esp_err_t time_wait_for_next_second(uint32_t timeout_ms);

/**
 * @brief 精确休眠指定秒数（不保证对齐）。
 * @param seconds 秒数。
 */
void time_sleep_seconds(uint32_t seconds);

#ifdef __cplusplus
}
#endif
