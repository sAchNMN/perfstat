/*
 * ========================================================================
 * 模块：app / collect_command
 * ========================================================================
 * 作用：调试子命令 collect 的入口 —— 前台运行采集骨架并打印榜单
 * 依据：docs/DESIGN.md 13（M1）
 */
#pragma once

#include <string>
#include <vector>

namespace perfstat::app {

// 参数 args[0] 固定为 "collect"，其余为选项
int run_collect_command(const std::vector<std::string>& args);

}  // namespace perfstat::app
