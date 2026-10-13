/*
 * ========================================================================
 * 模块：app / console_report
 * ========================================================================
 * 作用：M1 验收用的控制台输出 —— 按 CPU 排序的进程 Top 榜
 * 依据：docs/DESIGN.md 13（M1 验收标准）
 */
#pragma once

#include <cstddef>

#include "collect/process_metrics.h"

namespace perfstat {

struct ReportOptions {
    std::size_t top_n{20};           // 榜单条数
    bool        include_system{false};  // 是否把系统关键进程纳入榜单
    bool        only_background{false}; // 只显示判定为后台的进程
};

class ConsoleReporter {
public:
    explicit ConsoleReporter(ReportOptions options = {});

    // 渲染一轮：clear_screen 为真时清屏重绘
    void render(const ProcessSampleList& samples, const SampleContext& context, bool clear_screen);

private:
    ReportOptions options_;
};

}  // namespace perfstat
