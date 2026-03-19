#pragma once
#include <stdbool.h>

// 下载动画文件，返回是否成功
bool animation_downloader_download(const char *keyword, char *local_path, int path_len);
// 可扩展：支持进度回调、错误码等
