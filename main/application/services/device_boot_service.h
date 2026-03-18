#pragma once

#include "application/ports/device_boot_ports.h"
#include "esp_err.h"

esp_err_t device_boot_finalize(const device_boot_ports_t *ports);
