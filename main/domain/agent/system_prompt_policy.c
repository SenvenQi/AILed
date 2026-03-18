#include "system_prompt_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *agent_system_prompt_merge_with_context(const char *base_prompt,
                                             const char *soul_md,
                                             const char *user_md)
{
    const char *base = base_prompt ? base_prompt : "";
    const char *soul = soul_md ? soul_md : "";
    const char *user = user_md ? user_md : "";

    size_t len = strlen(base) + strlen(soul) + strlen(user) + 64;
    char *merged = calloc(1, len);
    if (!merged) {
        return NULL;
    }

    snprintf(merged, len, "%s\n\n[SOUL]\n%s\n\n[USER]\n%s", base, soul, user);
    return merged;
}
