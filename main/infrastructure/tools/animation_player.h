#pragma once
#include <stdbool.h>

// 播放本地动画文件，返回是否成功
bool animation_player_play(const char *local_path);
// 可扩展：支持停止、暂停、循环等
