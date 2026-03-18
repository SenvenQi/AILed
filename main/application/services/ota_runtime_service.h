#pragma once

#include "domain/ports/ota_runtime_ports.h"
#include "esp_err.h"

esp_err_t ota_runtime_boot(const ota_runtime_ports_t *ports);
