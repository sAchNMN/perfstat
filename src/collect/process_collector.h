/*
 * ========================================================================
 * 模块：collect / process_collector
 * ========================================================================
 * 作用：按轮次枚举全部进程并计算占用指标
 * 依据：docs/DESIGN.md 6.1（采集方法）/ D6（采集路线）/ D7（统计口径）
 * 说明：
 *   - 采集线程只做枚举与计算，不碰磁盘
 *   - 句柄用完立即关闭
 */
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "collect/background_filter.h"
#include "collect/process_metrics.h"

namespace perfstat {

class ProcessCollector {
public:
    ProcessCollector();

    /*
     * ====================================================================
     * 步骤：采集一轮
     * ====================================================================
     * 返回：本轮全部进程样本（含首轮无 delta 的 0 值样本）
     * 副作用：更新内部上一轮基准表与时间基准
     */
    ProcessSampleList collect_round();

    std::uint32_t logical_cores() const { return cores_; }

private:
    // 上一轮基准，用于差分算 CPU 与 IO 增量
    struct PrevState {
        std::uint64_t kernel_100ns{0};
        std::uint64_t user_100ns{0};
        std::uint64_t io_read{0};
        std::uint64_t io_write{0};
        std::string   instance_key;
    };
    using PrevMap = std::unordered_map<std::uint32_t, PrevState>;

    // 读取单个进程的全部指标并填充 delta
    void read_process(ProcessSample& sample, double wall_seconds, PrevMap& next);

    // 打分类标记（后台 / 服务宿主 / 系统关键）
    void classify(ProcessSample& sample);

    double current_wall_seconds();  // 取 QPC 与上一轮的间隔（秒），首轮返回 0

    std::uint32_t cores_{1};
    PrevMap       prev_;
    BackgroundFilter filter_;

    std::int64_t last_qpc_{0};
    double       qpc_freq_{1.0};
    bool         first_round_{true};
};

}  // namespace perfstat
