/*
 * ========================================================================
 * 模块：collect / background_filter
 * ========================================================================
 * 作用：实现 D10「后台进程」判定与系统关键进程黑名单
 * 判定式：
 *   is_background = (无可见顶层窗口 ∨ 属于服务宿主)
 *                   且 进程名 ∉ 系统关键进程黑名单
 * 说明：
 *   - 服务宿主集合由 M4 服务线注入；M1 阶段保持为空
 */
#pragma once

#include <cstdint>
#include <set>
#include <string>

namespace perfstat {

class BackgroundFilter {
public:
    BackgroundFilter();

    /*
     * ====================================================================
     * 步骤1：枚举当前拥有可见顶层窗口的 PID，刷新内部集合
     * ====================================================================
     * 数据源：EnumWindows + IsWindowVisible + GetWindowThreadProcessId
     */
    void refresh_visible_windows();

    // 步骤2：注入服务宿主 PID 集合（M4 接入）
    void set_service_host_pids(std::set<std::uint32_t> pids);

    bool has_visible_window(std::uint32_t pid) const;
    bool is_service_host(std::uint32_t pid) const;
    bool is_system_critical(const std::string& name) const;

    // D10 判定式入口
    bool is_background(std::uint32_t pid, const std::string& name) const;

    // 系统关键进程黑名单（全部小写）
    static const std::set<std::string>& blacklist();

private:
    std::set<std::uint32_t> window_pids_;         // 有可见顶层窗口的 PID
    std::set<std::uint32_t> service_host_pids_;   // 服务宿主 PID
};

}  // namespace perfstat
