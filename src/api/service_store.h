/*
 * ========================================================================
 * 模块：api / service_store
 * ========================================================================
 * 作用：在采集线程与 HTTP 线程之间传递「最新一轮服务快照」
 * 依据：docs/DESIGN.md 第 4 节（HTTP 线程只读内存）
 * 说明：与 snapshot_store 对称，仅承载服务列表
 */
#pragma once

#include <cstdint>
#include <mutex>

#include "collect/service_collector.h"

namespace perfstat::api {

struct ServiceSnapshot {
    std::int64_t      ts{0};             // 采集时刻（unix 秒）
    std::uint64_t     round{0};          // 第几轮
    ServiceInfoList   services;          // 全部服务快照
};

class ServiceStore {
public:
    void update(ServiceSnapshot snapshot);
    ServiceSnapshot get() const;
    bool has_data() const;

private:
    mutable std::mutex mu_;
    ServiceSnapshot    latest_;
    bool               valid_{false};
};

}  // namespace perfstat::api
