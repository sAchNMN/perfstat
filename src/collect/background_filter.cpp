/*
 * ========================================================================
 * 模块：collect / background_filter
 * ========================================================================
 * 作用：D10 后台判定与系统关键进程黑名单的实现
 */
#include "collect/background_filter.h"

#include <windows.h>

#include <algorithm>
#include <cctype>

namespace perfstat {
namespace {

// 把字符串转小写，用于与黑名单做大小写无关匹配
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// EnumWindows 回调上下文
struct WindowEnumCtx {
    std::set<std::uint32_t>* pids{nullptr};
};

// 收集拥有可见顶层窗口的 PID
BOOL CALLBACK collect_window_pids(HWND hwnd, LPARAM lparam) {
    if (!IsWindowVisible(hwnd)) {
        return TRUE;  // 不可见窗口直接跳过
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0) {
        auto* ctx = reinterpret_cast<WindowEnumCtx*>(lparam);
        ctx->pids->insert(static_cast<std::uint32_t>(pid));
    }
    return TRUE;
}

}  // namespace

BackgroundFilter::BackgroundFilter() = default;

/*
 * ========================================================================
 * 步骤1：刷新可见顶层窗口 PID 集合
 * ========================================================================
 * 目标集合：window_pids_
 * 操作：
 *   1) 清空旧集合
 *   2) 遍历所有顶层窗口，取可见者所属 PID
 */
void BackgroundFilter::refresh_visible_windows() {
    window_pids_.clear();

    // 1.1 枚举并收集
    WindowEnumCtx ctx{&window_pids_};
    EnumWindows(&collect_window_pids, reinterpret_cast<LPARAM>(&ctx));
}

void BackgroundFilter::set_service_host_pids(std::set<std::uint32_t> pids) {
    service_host_pids_ = std::move(pids);
}

bool BackgroundFilter::has_visible_window(std::uint32_t pid) const {
    return window_pids_.count(pid) != 0;
}

bool BackgroundFilter::is_service_host(std::uint32_t pid) const {
    return service_host_pids_.count(pid) != 0;
}

bool BackgroundFilter::is_system_critical(const std::string& name) const {
    return blacklist().count(to_lower(name)) != 0;
}

bool BackgroundFilter::is_background(std::uint32_t pid, const std::string& name) const {
    // 无可见窗口 或 服务宿主 -> 候选后台
    const bool candidate = !has_visible_window(pid) || is_service_host(pid);
    // 黑名单内的系统关键进程一律不算后台
    return candidate && !is_system_critical(name);
}

const std::set<std::string>& BackgroundFilter::blacklist() {
    // 依据 D10：系统关键进程，默认屏蔽，前端可由「显示系统进程」放行
    static const std::set<std::string> kBlacklist = {
        "system",
        "system idle process",
        "[system process]",
        "registry",
        "memory compression",
        "secure system",
        "csrss.exe",
        "wininit.exe",
        "smss.exe",
        "dwm.exe",
        "services.exe",
        "lsass.exe",
    };
    return kBlacklist;
}

}  // namespace perfstat
