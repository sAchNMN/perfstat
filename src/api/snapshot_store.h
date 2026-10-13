/*
 * ========================================================================
 * 模块：api / snapshot_store
 * ========================================================================
 * 作用：在采集线程与 HTTP 线程之间传递「最新一轮快照」
 * 依据：docs/DESIGN.md 第 4 节（HTTP 线程只读内存）
 * 说明：
 *   - 写方只有采集线程，读方是各 HTTP 工作线程
 *   - 只保留最新一轮，历史趋势由 ring_buffer（后续）承担
 */
#pragma once

#include <cstdint>
#include <mutex>

#include "collect/process_metrics.h"

namespace perfstat::api {

struct Snapshot {
    std::int64_t      ts{0};              // 采集时刻（unix 秒）
    std::uint64_t     round{0};           // 第几轮
    std::uint32_t     logical_cores{1};   // 逻辑核数
    ProcessSampleList processes;          // 全部进程样本
};

class SnapshotStore {
public:
    // 采集线程调用：整帧替换
    void update(Snapshot snapshot);

    // HTTP 线程调用：取一份拷贝
    Snapshot get() const;

    bool has_data() const;

private:
    mutable std::mutex mu_;
    Snapshot           latest_;
    bool               valid_{false};
};

}  // namespace perfstat::api
