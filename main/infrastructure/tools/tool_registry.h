#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"

#define TOOL_NAME_MAX       32
#define TOOL_DESC_MAX       128
#define TOOL_PARAM_MAX      8
#define TOOL_REGISTRY_MAX   16

typedef enum {
    TOOL_PARAM_TYPE_STRING,
    TOOL_PARAM_TYPE_INT,
    TOOL_PARAM_TYPE_NUMBER,
    TOOL_PARAM_TYPE_BOOL,
    TOOL_PARAM_TYPE_ARRAY,
} tool_param_type_t;

#define TOOL_PARAM_TYPE_FLOAT TOOL_PARAM_TYPE_NUMBER

typedef struct {
    const char *name;
    const char *description;
    tool_param_type_t type;
    bool required;
    const char **enum_values;
    int enum_count;
    bool has_minimum;
    bool has_maximum;
    double minimum;
    double maximum;
    tool_param_type_t items_type;
    const char *items_description;
} tool_param_desc_t;

typedef cJSON *(*tool_handler_t)(const cJSON *params);

typedef struct {
    const char *name;
    const char *description;
    tool_param_desc_t params[TOOL_PARAM_MAX];
    int param_count;
    tool_handler_t handler;
    bool strict;
} tool_def_t;

typedef struct {
    tool_def_t tools[TOOL_REGISTRY_MAX];
    bool tool_enabled[TOOL_REGISTRY_MAX];
    int tool_count;
} ToolRegistry;

// 默认全局实例
extern ToolRegistry g_tool_registry;

// 新增实例化操作接口
void tool_registry_init_ex(ToolRegistry *reg);
esp_err_t tool_registry_register_ex(ToolRegistry *reg, const tool_def_t *tool);
const tool_def_t *tool_registry_find_ex(ToolRegistry *reg, const char *name);
cJSON *tool_registry_call_ex(ToolRegistry *reg, const char *name, const cJSON *params);
cJSON *tool_registry_get_schema_ex(ToolRegistry *reg);
int tool_registry_count_ex(ToolRegistry *reg);

// 兼容旧接口（默认全局实例）
void tool_registry_init(void);
esp_err_t tool_registry_register(const tool_def_t *tool);
const tool_def_t *tool_registry_find(const char *name);
cJSON *tool_registry_call(const char *name, const cJSON *params);
cJSON *tool_registry_get_schema(void);
int tool_registry_count(void);
void tool_registry_register_builtins(void);
esp_err_t tool_registry_apply_config_markdown(const char *markdown_text);
esp_err_t tool_registry_load_config_from_file(const char *file_path);
