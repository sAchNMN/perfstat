/*
 * ========================================================================
 * 模块：store / aggregator
 * ========================================================================
 * 作用：分钟桶聚合实现
 */
#include "store/aggregator.h"

#include <utility>

namespace perfstat::store {
namespace {

// 分钟对齐：向下取整到整分钟
std::int64_t align_to_minute(std::int64_t ts) { return ts - (ts % 60); }

}  // namespace

bool MinuteAggregator::add_round(const ProcessSampleList& samples, std::int64_t now_ts,
                                 MinuteBucket* out) {
    const std::int64_t minute = align_to_minute(now_ts);
    bool produced = false;

    /*
     * ========================================================================
     * 步骤1：判断分钟边界
     * ========================================================================
     * 说明：跨分钟时把上一分钟的累加结果整桶交出，再重置累加器
     */
    if (!started_) {
        started_ = true;
        current_bucket_ts_ = minute;
    } else if (minute != current_bucket_ts_) {
        if (out != nullptr) {
            *out = make_bucket(current_bucket_ts_);
            produced = true;
        }
        current_.clear();
        current_bucket_ts_ = minute;
    }

    /*
     * ========================================================================
     * 步骤2：把本轮样本并入当前分钟累加器
     * ========================================================================
     * 过滤：
     *   1) accessible 为假（句柄打不开）跳过
     *   2) is_background 为假（前台或系统关键）跳过
     */
    for (const ProcessSample& sample : samples) {
        if (!sample.accessible || !sample.is_background) {
            continue;
        }

        const std::string key =
            sample.instance_key.empty() ? sample.name : sample.instance_key;
        Accum& acc = current_[key];

        // 2.1 首次出现时记录实体信息
        if (acc.count == 0) {
            acc.entity.kind = "process";
            acc.entity.name = sample.name;
            acc.entity.path = sample.path;
            acc.entity.instance_key = key;
        }

        // 2.2 CPU：累加求均值，另记峰值
        acc.cpu_sum += sample.cpu_percent;
        if (sample.cpu_percent > acc.cpu_max) {
            acc.cpu_max = sample.cpu_percent;
        }

        // 2.3 内存：工作集 KB，累加求均值，另记峰值
        const double mem_kb = static_cast<double>(sample.working_set_bytes) / 1024.0;
        acc.mem_sum_kb += mem_kb;
        if (static_cast<std::int64_t>(mem_kb) > acc.mem_max_kb) {
            acc.mem_max_kb = static_cast<std::int64_t>(mem_kb);
        }

        // 2.4 IO：区间增量累加
        acc.io_read_kb += static_cast<std::int64_t>(sample.io_read_delta / 1024);
        acc.io_write_kb += static_cast<std::int64_t>(sample.io_write_delta / 1024);

        acc.count += 1;
    }

    return produced;
}

MinuteBucket MinuteAggregator::flush() {
    /*
     * ========================================================================
     * 步骤1：产出当前桶并清空累加器
     * ========================================================================
     * 说明：退出时可能不足一分钟，sample_count 会小于常态值，属预期
     */
    MinuteBucket bucket = make_bucket(current_bucket_ts_);
    current_.clear();
    return bucket;
}

MinuteBucket MinuteAggregator::make_bucket(std::int64_t bucket_ts) const {
    /*
     * ========================================================================
     * 步骤1：把每个实体的累加值折算成一行指标
     * ========================================================================
     * 折算：
     *   cpu_avg = cpu_sum / count
     *   mem_avg = mem_sum_kb / count
     *   io_*    = 区间增量累加（不做均值）
     */
    MinuteBucket bucket;
    bucket.bucket_ts = bucket_ts;
    bucket.rows.reserve(current_.size());

    for (const auto& entry : current_) {
        const Accum& acc = entry.second;
        if (acc.count == 0) {
            continue;
        }

        PendingRow row;
        row.entity = acc.entity;
        row.metric.bucket_ts = bucket_ts;
        row.metric.cpu_avg = acc.cpu_sum / acc.count;
        row.metric.cpu_max = acc.cpu_max;
        row.metric.mem_avg_kb = static_cast<std::int64_t>(acc.mem_sum_kb / acc.count);
        row.metric.mem_max_kb = acc.mem_max_kb;
        row.metric.io_read_kb = acc.io_read_kb;
        row.metric.io_write_kb = acc.io_write_kb;
        row.metric.sample_count = acc.count;

        bucket.rows.push_back(std::move(row));
    }

    return bucket;
}

}  // namespace perfstat::store
