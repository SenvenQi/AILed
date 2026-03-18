#pragma once

#include <stdbool.h>

/* Domain policy: system context publishing failure should not block boot. */
bool boot_policy_allow_context_publish_failure(void);
