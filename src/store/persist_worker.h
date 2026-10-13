/*
 * ========================================================================
 * 模块：store / persist_worker
 * ========================================================================
 * 作用：写库线程 —— 消费分钟桶，落库并做保留清理
 * 依据：docs/DESIGN.md 第 4 节（分层原则）/ 6.4
 * 说明：
 *   - 采集线程 submit() 入队即返回，绝不因磁盘 IO 阻塞
 *   - 所有 sqlite 调用都发生在本线程，数据库连接无跨线程并发
 *   - 队列有上限，溢出时丢最老桶并计数，防止异常场景内存膨胀
 */
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "store/db.h"
#include "store/store_types.h"

namespace perfstat::store {

class PersistWorker {
public:
    PersistWorker() = default;
    ~PersistWorker();

    PersistWorker(const PersistWorker&) = delete;
    PersistWorker& operator=(const PersistWorker&) = delete;

    /*
     * ====================================================================
     * 步骤：启动写库线程
     * ====================================================================
     * 操作：打开数据库 -> 启动清理 -> 起线程
     */
    bool start(const std::string& db_path, int retain_days, std::string* err);

    // 停止并排空队列
    void stop();

    // 采集线程调用：入队一个已完成分钟桶
    void submit(MinuteBucket bucket);

    std::int64_t written_rows() const;
    std::int64_t dropped_buckets() const;
    std::int64_t purged_rows() const;

private:
    // 队列上限（桶），超出丢最老
    static constexpr std::size_t kMaxQueuedBuckets = 64;
    // 清理周期（秒）
    static constexpr std::int64_t kPurgeIntervalSeconds = 24 * 3600;

    void loop();
    void persist(MinuteBucket& bucket);
    void purge_once(std::int64_t now_ts);

    mutable std::mutex       mu_;
    std::condition_variable  cv_;
    std::deque<MinuteBucket> queue_;
    bool                     running_{false};
    bool                     stop_requested_{false};
    std::thread              thread_;

    Database     db_;
    int          retain_days_{30};
    std::int64_t last_purge_ts_{0};

    std::int64_t written_rows_{0};
    std::int64_t dropped_buckets_{0};
    std::int64_t purged_rows_{0};
};

}  // namespace perfstat::store
