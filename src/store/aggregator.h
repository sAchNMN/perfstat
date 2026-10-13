/*
 * ========================================================================
 * 模块：store / aggregator
 * ========================================================================
 * 作用：把多轮采样点按「分钟桶 × 实体」累加，产出 fact_metric_1m 行
 * 依据：docs/DESIGN.md 6.4（存储与聚合）
 * 说明：
 *   - 只统计 D10 判定为「后台」且指标可读的进程
 *   - 纯内存计算，不碰磁盘，供采集线程直接调用
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "collect/process_metrics.h"
#include "store/store_types.h"

namespace perfstat::store {

class MinuteAggregator {
public:
    /*
     * ====================================================================
     * 步骤：并入一轮采样
     * ====================================================================
     * 返回：true 表示跨越分钟边界，out 已被写入上一分钟的完整桶
     */
    bool add_round(const ProcessSampleList& samples, std::int64_t now_ts, MinuteBucket* out);

    // 强制产出当前累积桶（程序退出收尾用）
    MinuteBucket flush();

    bool empty() const { return current_.empty(); }

private:
    // 单个实体在当分钟内的累加器
    struct Accum {
        EntityRow    entity;
        double       cpu_sum{0.0};
        double       cpu_max{0.0};
        double       mem_sum_kb{0.0};
        std::int64_t mem_max_kb{0};
        std::int64_t io_read_kb{0};
        std::int64_t io_write_kb{0};
        int          count{0};
    };

    MinuteBucket make_bucket(std::int64_t bucket_ts) const;

    std::map<std::string, Accum> current_;  // key = instance_key
    std::int64_t current_bucket_ts_{0};
    bool         started_{false};
};

}  // namespace perfstat::store
