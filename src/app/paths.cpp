/*
 * ========================================================================
 * 模块：app / paths
 * ========================================================================
 * 作用：%LOCALAPPDATA%\PerfStat\ 路径解析与目录创建
 * 说明：只用 Win32 API，不引 shell32，避免多余链接依赖
 */
#include "app/paths.h"

#include <windows.h>

namespace perfstat::app {
namespace {

constexpr const char* kAppFolder = "PerfStat";

// 拼接路径子项，自动补分隔符
std::string join(const std::string& base, const char* child) {
    if (base.empty()) {
        return std::string(child);
    }
    if (base.back() == '\\' || base.back() == '/') {
        return base + child;
    }
    return base + "\\" + child;
}

// 创建单层目录；已存在视为成功
bool create_dir(const std::string& path, std::string* err) {
    if (CreateDirectoryA(path.c_str(), nullptr)) {
        return true;
    }
    const DWORD code = GetLastError();
    if (code == ERROR_ALREADY_EXISTS) {
        return true;
    }
    if (err != nullptr) {
        *err = "创建目录失败：" + path + "（err=" + std::to_string(code) + "）";
    }
    return false;
}

}  // namespace

std::string data_dir() {
    /*
     * ========================================================================
     * 步骤1：读取 %LOCALAPPDATA% 并拼出应用子目录
     * ========================================================================
     */
    char buffer[MAX_PATH] = {0};
    const DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::string();
    }
    return join(std::string(buffer), kAppFolder);
}

std::string db_path() { return join(data_dir(), "perfstat.db"); }

std::string logs_dir() { return join(data_dir(), "logs"); }

std::string config_path() { return join(data_dir(), "config.json"); }

bool ensure_data_dirs(std::string* err) {
    /*
     * ========================================================================
     * 步骤1：创建根目录与 logs 子目录
     * ========================================================================
     */
    const std::string root = data_dir();
    if (root.empty()) {
        if (err != nullptr) {
            *err = "无法解析 %LOCALAPPDATA%";
        }
        return false;
    }
    return create_dir(root, err) && create_dir(logs_dir(), err);
}

}  // namespace perfstat::app
