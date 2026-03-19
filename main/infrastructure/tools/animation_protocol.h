#pragma once
#include <stdbool.h>

// 动画云端检索与下载协议相关定义
// 可根据实际服务器接口调整

#define ANIMATION_SERVER_URL "http://your-server/api/animation/search"
#define ANIMATION_DOWNLOAD_URL_FMT "http://your-server/api/animation/download?file=%s"

// 服务器返回的动画元数据结构（示例）
typedef struct {
    char file_url[256]; // 动画文件下载链接
    char name[64];     // 动画名称
    int frame_count;   // 帧数
    int fps;           // 帧率
} animation_meta_t;

// 解析服务器响应，获取动画元数据
// 返回true表示解析成功
bool animation_protocol_parse_meta(const char *json, animation_meta_t *meta);
