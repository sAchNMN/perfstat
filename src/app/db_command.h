/*
 * ========================================================================
 * 模块：app / db_command
 * ========================================================================
 * 作用：调试子命令 db —— 查看/维护本地数据库（M2 验收入口）
 * 依据：docs/DESIGN.md 13（M2 存储层）
 */
#pragma once

#include <string>
#include <vector>

namespace perfstat::app {

// 参数 args[0] 固定为 "db"，其余为选项
int run_db_command(const std::vector<std::string>& args);

}  // namespace perfstat::app
