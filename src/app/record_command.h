/*
 * ========================================================================
 * 模块：app / record_command
 * ========================================================================
 * 作用：调试子命令 record —— 采集并落库（M2 验收入口）
 * 依据：docs/DESIGN.md 13（M2 存储层）
 */
#pragma once

#include <string>
#include <vector>

namespace perfstat::app {

// 参数 args[0] 固定为 "record"，其余为选项
int run_record_command(const std::vector<std::string>& args);

}  // namespace perfstat::app
