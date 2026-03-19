#include "animation_protocol.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "cJSON.h"

// 解析服务器返回的JSON，提取动画元数据
bool animation_protocol_parse_meta(const char *json, animation_meta_t *meta) {
    if (!json || !meta) return false;
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *file_url = cJSON_GetObjectItem(root, "file_url");
    cJSON *name = cJSON_GetObjectItem(root, "name");
    cJSON *frame_count = cJSON_GetObjectItem(root, "frame_count");
    cJSON *fps = cJSON_GetObjectItem(root, "fps");
    if (!cJSON_IsString(file_url) || !cJSON_IsString(name) || !cJSON_IsNumber(frame_count) || !cJSON_IsNumber(fps)) {
        cJSON_Delete(root);
        return false;
    }
    strncpy(meta->file_url, file_url->valuestring, sizeof(meta->file_url) - 1);
    strncpy(meta->name, name->valuestring, sizeof(meta->name) - 1);
    meta->frame_count = frame_count->valueint;
    meta->fps = fps->valueint;
    cJSON_Delete(root);
    return true;
}
