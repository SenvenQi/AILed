#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "tool_registry.h"
#include "esp_log.h"

static const char *TAG = "tool_registry";

ToolRegistry g_tool_registry = {0};

// --- 实例化操作接口 ---
void tool_registry_init_ex(ToolRegistry *reg) {
    if (!reg) return;
    reg->tool_count = 0;
    memset(reg->tools, 0, sizeof(reg->tools));
    memset(reg->tool_enabled, 0, sizeof(reg->tool_enabled));
    ESP_LOGI(TAG, "Tool registry initialized (instance)");
}
// 去除字符串首尾空白字符，原地修改，返回去除后的指针
char *trim_inplace(char *s) {
    if (!s) return s;
    // 去除前导空白
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    // 去除尾部空白
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
    return s;
}

// 解析 "on"/"off"/"true"/"false"/"1"/"0" 等字符串为 bool
bool parse_enabled_token(const char *value, bool *enabled) {
    if (!value || !enabled) return false;
    if (strcasecmp(value, "on") == 0 || strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        *enabled = true;
        return true;
    }
    if (strcasecmp(value, "off") == 0 || strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *enabled = false;
        return true;
    }
    return false;
}

static int tool_registry_index_of(ToolRegistry *reg, const char *name) {
    if (!reg || !name) return -1;
    for (int i = 0; i < reg->tool_count; i++) {
        if (strcmp(reg->tools[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

esp_err_t tool_registry_register_ex(ToolRegistry *reg, const tool_def_t *tool) {
    if (!reg || !tool || !tool->name || !tool->handler) return ESP_ERR_INVALID_ARG;
    if (reg->tool_count >= TOOL_REGISTRY_MAX) return ESP_ERR_NO_MEM;
    if (tool_registry_find_ex(reg, tool->name)) return ESP_ERR_INVALID_STATE;
    reg->tools[reg->tool_count] = *tool;
    reg->tool_enabled[reg->tool_count] = true;
    reg->tool_count++;
    ESP_LOGI(TAG, "Registered tool: %s (instance)", tool->name);
    return ESP_OK;
}

const tool_def_t *tool_registry_find_ex(ToolRegistry *reg, const char *name) {
    if (!reg || !name) return NULL;
    for (int i = 0; i < reg->tool_count; i++) {
        if (strcmp(reg->tools[i].name, name) == 0) {
            return &reg->tools[i];
        }
    }
    return NULL;
}

cJSON *tool_registry_call_ex(ToolRegistry *reg, const char *name, const cJSON *params) {
    int idx = tool_registry_index_of(reg, name);
    if (idx < 0) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "tool not found");
        return err;
    }
    if (!reg->tool_enabled[idx]) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "tool disabled by config");
        cJSON_AddStringToObject(err, "tool", name);
        return err;
    }
    const tool_def_t *tool = &reg->tools[idx];
    ESP_LOGI(TAG, "Calling tool: %s (instance)", name);
    cJSON *result = tool->handler(params);
    if (!result) {
        result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "error", "tool returned null");
    }
    return result;
}

static const char *param_type_to_str(tool_param_type_t type) {
    switch (type) {
    case TOOL_PARAM_TYPE_STRING:  return "string";
    case TOOL_PARAM_TYPE_INT:     return "integer";
    case TOOL_PARAM_TYPE_NUMBER:  return "number";
    case TOOL_PARAM_TYPE_BOOL:    return "boolean";
    case TOOL_PARAM_TYPE_ARRAY:   return "array";
    default:                     return "string";
    }
}

cJSON *tool_registry_get_schema_ex(ToolRegistry *reg) {
    cJSON *tools_array = cJSON_CreateArray();
    if (!reg) return tools_array;
    for (int i = 0; i < reg->tool_count; i++) {
        if (!reg->tool_enabled[i]) continue;
        const tool_def_t *t = &reg->tools[i];
        cJSON *func_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(func_obj, "type", "function");
        cJSON *func = cJSON_CreateObject();
        cJSON_AddStringToObject(func, "name", t->name);
        if (t->description) cJSON_AddStringToObject(func, "description", t->description);
        if (t->strict) cJSON_AddBoolToObject(func, "strict", true);
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON *props = cJSON_CreateObject();
        cJSON *required = cJSON_CreateArray();
        for (int p = 0; p < t->param_count; p++) {
            const tool_param_desc_t *pd = &t->params[p];
            cJSON *prop = cJSON_CreateObject();
            cJSON_AddStringToObject(prop, "type", param_type_to_str(pd->type));
            if (pd->description) cJSON_AddStringToObject(prop, "description", pd->description);
            if (pd->enum_values && pd->enum_count > 0) {
                cJSON *enum_arr = cJSON_CreateArray();
                for (int e = 0; e < pd->enum_count; e++) {
                    cJSON_AddItemToArray(enum_arr, cJSON_CreateString(pd->enum_values[e]));
                }
                cJSON_AddItemToObject(prop, "enum", enum_arr);
            }
            if (pd->has_minimum) cJSON_AddNumberToObject(prop, "minimum", pd->minimum);
            if (pd->has_maximum) cJSON_AddNumberToObject(prop, "maximum", pd->maximum);
            cJSON_AddItemToObject(props, pd->name, prop);
            if (pd->required) cJSON_AddItemToArray(required, cJSON_CreateString(pd->name));
        }
        cJSON_AddItemToObject(params, "properties", props);
        if (cJSON_GetArraySize(required) > 0) cJSON_AddItemToObject(params, "required", required);
        cJSON_AddItemToObject(func, "parameters", params);
        cJSON_AddItemToObject(func_obj, "function", func);
        cJSON_AddItemToArray(tools_array, func_obj);
    }
    return tools_array;
}

int tool_registry_count_ex(ToolRegistry *reg) {
    return reg ? reg->tool_count : 0;
}

// --- 兼容旧接口（全局实例） ---
void tool_registry_init(void) { tool_registry_init_ex(&g_tool_registry); }
esp_err_t tool_registry_register(const tool_def_t *tool) { return tool_registry_register_ex(&g_tool_registry, tool); }
const tool_def_t *tool_registry_find(const char *name) { return tool_registry_find_ex(&g_tool_registry, name); }
cJSON *tool_registry_call(const char *name, const cJSON *params) { return tool_registry_call_ex(&g_tool_registry, name, params); }
cJSON *tool_registry_get_schema(void) { return tool_registry_get_schema_ex(&g_tool_registry); }
int tool_registry_count(void) { return tool_registry_count_ex(&g_tool_registry); }
esp_err_t tool_registry_apply_config_markdown(const char *markdown_text)
{
    if (markdown_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *buf = strdup(markdown_text);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int applied = 0;
    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\r\n", &saveptr);
         line != NULL;
         line = strtok_r(NULL, "\r\n", &saveptr)) {
        char *p = trim_inplace(line);
        if (*p == '\0' || *p == '#') {
            continue;
        }

        if (p[0] == '-' || p[0] == '*') {
            p++;
            p = trim_inplace(p);
        }

        char *colon = strchr(p, ':');
        if (colon == NULL) {
            continue;
        }
        *colon = '\0';

        char *name = trim_inplace(p);
        char *value = trim_inplace(colon + 1);
        if (*name == '\0' || *value == '\0') {
            continue;
        }

        bool enabled = true;
        if (!parse_enabled_token(value, &enabled)) {
            continue;
        }

        int idx = tool_registry_index_of(&g_tool_registry, name);
        if (idx < 0) {
            ESP_LOGW(TAG, "Unknown tool in markdown config: %s", name);
            continue;
        }

        g_tool_registry.tool_enabled[idx] = enabled;
        applied++;
    }

    free(buf);
    ESP_LOGI(TAG, "Applied markdown skill config entries: %d", applied);
    return ESP_OK;
}

esp_err_t tool_registry_load_config_from_file(const char *file_path)
{
    if (file_path == NULL || file_path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *fp = fopen(file_path, "rb");
    if (fp == NULL) {
        ESP_LOGW(TAG, "Skill config file not found: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return ESP_FAIL;
    }

    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return ESP_FAIL;
    }
    rewind(fp);

    char *buf = calloc(1, (size_t)sz + 1);
    if (buf == NULL) {
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';

    esp_err_t ret = tool_registry_apply_config_markdown(buf);
    free(buf);
    return ret;
}
