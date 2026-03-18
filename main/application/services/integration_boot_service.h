#pragma once

#include "application/ports/integration_boot_ports.h"
#include "esp_err.h"

esp_err_t integration_boot_start(const integration_boot_ports_t *ports);
