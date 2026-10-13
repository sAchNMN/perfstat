/*
 * ========================================================================
 * 模块：api / startup_store
 * ========================================================================
 * 作用：在采集线程与 HTTP 线程之间传递「最新一份启动项清单」
 * 依据：docs/DESIGN.md 第 4 节（HTTP 线程只读内存）/ 6.6
 * 说明：
 *   - 与 snapshot_store / service_store 对称
 *   - 启动项变化很慢（用户装软件才动），采集侧每 15 轮（约 30s）刷新一次
 *   - 这里只存静态清单；「当前是否在跑、占多少」在序列化阶段用进程快照现算
 */
#pragma once

#include <cstdint>
#include <mutex>

#include "collect/startup_collector.h"

namespace perfstat::api {

struct StartupSnapshot {
    std::int64_t    ts{0};         // 采集时刻（unix 秒）
    StartupInfoList items;         // 全部自启项
};

class StartupStore {
public:
    void update(StartupSnapshot snapshot);
    StartupSnapshot get() const;
    bool has_data() const;

private:
    mutable std::mutex mu_;
    StartupSnapshot    latest_;
    bool               valid_{false};
};

}  // namespace perfstat::api
