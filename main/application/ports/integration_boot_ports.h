#pragma once

#include "esp_err.h"

typedef struct {
    esp_err_t (*init_bot)(void);
    esp_err_t (*start_bot)(void);
} integration_boot_ports_t;
