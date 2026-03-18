#pragma once

#include "esp_err.h"
#include "domain/ports/app_runtime_ports.h"

esp_err_t app_lifecycle_start(const app_runtime_ports_t *ports);
