/*
 * ========================================================================
 * 模块：app / paths
 * ========================================================================
 * 作用：解析本地数据目录
 * 依据：docs/DESIGN.md 7（落盘位置）——%LOCALAPPDATA%\PerfStat\
 */
#pragma once

#include <string>

namespace perfstat::app {

// 数据根目录（%LOCALAPPDATA%\PerfStat）；解析失败返回空串
std::string data_dir();

// 各具体落点
std::string db_path();
std::string logs_dir();
std::string config_path();

/*
 * ========================================================================
 * 步骤：确保数据目录存在
 * ========================================================================
 * 目标：根目录 + logs 子目录
 * 返回：false 时 err 带原因
 */
bool ensure_data_dirs(std::string* err);

}  // namespace perfstat::app
