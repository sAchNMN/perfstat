/*
 * ========================================================================
 * 模块：app / serve_command
 * ========================================================================
 * 作用：调试子命令 serve —— 常驻模式：采样 + 落库 + HTTP/SSE（M3 验收入口）
 * 依据：docs/DESIGN.md 5 / 13（M3）
 */
#pragma once

#include <string>
#include <vector>

namespace perfstat::app {

// 参数 args[0] 固定为 "serve"，其余为选项
int run_serve_command(const std::vector<std::string>& args);

}  // namespace perfstat::app
