#include "ddd_regression_checks.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "domain/agent/system_prompt_policy.h"
#include "domain/conversation/response_route_policy.h"
#include "domain/device/indicator_policy.h"
#include "domain/system/boot_policy.h"

esp_err_t ddd_regression_checks_run(void)
{
    if (!boot_policy_allow_context_publish_failure()) {
        return ESP_FAIL;
    }

    if (!device_indicator_should_clear_after_boot()) {
        return ESP_FAIL;
    }

    if (conversation_route_decide("chat", "msg") != CONVERSATION_ROUTE_REPLY_MESSAGE) {
        return ESP_FAIL;
    }
    if (conversation_route_decide("chat", "") != CONVERSATION_ROUTE_SEND_CHAT) {
        return ESP_FAIL;
    }
    if (conversation_route_decide("", "") != CONVERSATION_ROUTE_NONE) {
        return ESP_FAIL;
    }

    char *merged = agent_system_prompt_merge_with_context("BASE", "SOUL", "USER");
    if (!merged) {
        return ESP_ERR_NO_MEM;
    }

    bool ok = strstr(merged, "BASE") != NULL &&
              strstr(merged, "[SOUL]") != NULL &&
              strstr(merged, "[USER]") != NULL;
    free(merged);

    return ok ? ESP_OK : ESP_FAIL;
}
