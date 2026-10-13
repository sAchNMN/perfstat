/*
 * ========================================================================
 * 模块：collect / sampler
 * ========================================================================
 * 作用：用 Waitable Timer 驱动采集节拍，避免 Sleep 空转
 * 依据：docs/DESIGN.md 6.1 / 10（轻量化第 1 条）
 * 说明：
 *   - run() 阻塞运行，Ctrl+C 退出
 *   - 采集回调内只做枚举与计算，不碰磁盘
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "collect/process_collector.h"
#include "collect/process_metrics.h"

namespace perfstat {

class Sampler {
public:
    using Handler = std::function<void(const ProcessSampleList&, const SampleContext&)>;

    explicit Sampler(std::uint32_t interval_ms = 2000);

    // 阻塞运行，直到内部停止标志被置位
    void run(const Handler& handler);

    void request_stop();

    std::uint32_t logical_cores() const { return collector_.logical_cores(); }

private:
    std::uint32_t    interval_ms_;
    ProcessCollector collector_;
    std::atomic<bool> stop_{false};
};

}  // namespace perfstat
