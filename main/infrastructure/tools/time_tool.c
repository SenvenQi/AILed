#include "time_tool.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

esp_err_t tools_get_time(time_t *out_time)
{
    if (!out_time) return ESP_ERR_INVALID_ARG;
    time_t t = time(NULL);
    if (t == (time_t)-1) return ESP_FAIL;
    *out_time = t;
    return ESP_OK;
}

esp_err_t tools_gettimeofday(struct timeval *tv)
{
    if (!tv) return ESP_ERR_INVALID_ARG;
    int rc = gettimeofday(tv, NULL);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t tools_format_time_iso(time_t t, char *buf, size_t buf_len, bool utc)
{
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    struct tm tmres;
    struct tm *tm_ptr;

    if (utc) {
        tm_ptr = gmtime_r(&t, &tmres);
    } else {
        tm_ptr = localtime_r(&t, &tmres);
    }
    if (!tm_ptr) return ESP_FAIL;

    int n = snprintf(buf, buf_len, "%04d-%02d-%02d %02d:%02d:%02d",
                     tmres.tm_year + 1900,
                     tmres.tm_mon + 1,
                     tmres.tm_mday,
                     tmres.tm_hour,
                     tmres.tm_min,
                     tmres.tm_sec);
    if (n < 0) return ESP_FAIL;
    if ((size_t)n >= buf_len) return ESP_FAIL;
    return ESP_OK;
}
