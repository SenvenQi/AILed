#pragma once
#include "esp_err.h"
typedef struct {
    esp_err_t (*init_ota)(void);
} ota_runtime_ports_t;
