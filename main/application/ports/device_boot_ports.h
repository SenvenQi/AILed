#pragma once

#include "esp_err.h"

typedef struct {
    esp_err_t (*clear_status_indicator)(void);
} device_boot_ports_t;
