#pragma once

#include "domain/ports/agent_runtime_ports.h"
#include "esp_err.h"

esp_err_t agent_runtime_service_start(const agent_runtime_ports_t *ports);
