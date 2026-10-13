/*
 * ========================================================================
 * 模块：store / persist_worker
 * ========================================================================
 * 作用：写库线程实现
 */
#include "store/persist_worker.h"

#include <cstdio>
#include <ctime>
#include <utility>
#include <vector>

namespace perfstat::store {

PersistWorker::~PersistWorker() { stop(); }

bool PersistWorker::start(const std::string& db_path, int retain_days, std::string* err) {
    std::lock_guard<std::mutex> lock(mu_);
    if (running_) {
        return true;
    }

    /*
     * ========================================================================
     * 步骤1：打开数据库
     * ========================================================================
     */
    if (!db_.open(db_path, err)) {
        return false;
    }

    /*
     * ========================================================================
     * 步骤2：起写库线程
     * ========================================================================
     */
    retain_days_ = retain_days;
    stop_requested_ = false;
    running_ = true;
    thread_ = std::thread([this] { loop(); });

    std::fprintf(stderr, "[PerfStat][store] 写库线程已启动：%s（保留 %d 天）\n", db_path.c_str(),
                 retain_days);
    return true;
}

void PersistWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!running_) {
            return;
        }
        stop_requested_ = true;
    }
    cv_.notify_all();

    if (thread_.joinable()) {
        thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        running_ = false;
    }
    db_.close();
}

void PersistWorker::submit(MinuteBucket bucket) {
    if (bucket.rows.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mu_);
    if (!running_) {
        return;
    }

    /*
     * ========================================================================
     * 步骤1：队列限长，溢出丢最老
     * ========================================================================
     * 说明：正常节拍下队列深度恒为 0；只有磁盘持续卡住才会堆积
     */
    if (queue_.size() >= kMaxQueuedBuckets) {
        queue_.pop_front();
        dropped_buckets_ += 1;
    }

    queue_.push_back(std::move(bucket));
    cv_.notify_one();
}

std::int64_t PersistWorker::written_rows() const {
    std::lock_guard<std::mutex> lock(mu_);
    return written_rows_;
}

std::int64_t PersistWorker::dropped_buckets() const {
    std::lock_guard<std::mutex> lock(mu_);
    return dropped_buckets_;
}

std::int64_t PersistWorker::purged_rows() const {
    std::lock_guard<std::mutex> lock(mu_);
    return purged_rows_;
}

void PersistWorker::loop() {
    /*
     * ========================================================================
     * 步骤1：启动即做一次保留清理
     * ========================================================================
     */
    const std::int64_t start_ts = static_cast<std::int64_t>(std::time(nullptr));
    purge_once(start_ts);

    /*
     * ========================================================================
     * 步骤2：消费队列
     * ========================================================================
     * 退出条件：收到停止请求「且」队列已排空
     */
    std::unique_lock<std::mutex> lock(mu_);
    while (true) {
        cv_.wait(lock, [this] { return stop_requested_ || !queue_.empty(); });

        if (queue_.empty()) {
            if (stop_requested_) {
                break;
            }
            continue;
        }

        MinuteBucket bucket = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();

        persist(bucket);

        // 步骤3：周期性保留清理
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
        if (now - last_purge_ts_ >= kPurgeIntervalSeconds) {
            purge_once(now);
        }

        lock.lock();
    }
}

void PersistWorker::persist(MinuteBucket& bucket) {
    /*
     * ========================================================================
     * 步骤1：逐实体 upsert，回填 entity_id
     * ========================================================================
     */
    std::vector<MetricRow> metrics;
    metrics.reserve(bucket.rows.size());
    std::string err;

    for (PendingRow& row : bucket.rows) {
        const std::int64_t entity_id = db_.upsert_entity(row.entity, bucket.bucket_ts, &err);
        if (entity_id == 0) {
            std::fprintf(stderr, "[PerfStat][store] upsert 实体失败：%s\n", err.c_str());
            continue;
        }
        row.metric.entity_id = entity_id;
        row.metric.bucket_ts = bucket.bucket_ts;
        metrics.push_back(row.metric);
    }

    /*
     * ========================================================================
     * 步骤2：整批事实行单事务写入
     * ========================================================================
     */
    if (metrics.empty()) {
        return;
    }
    if (!db_.insert_metric_rows(metrics, &err)) {
        std::fprintf(stderr, "[PerfStat][store] 写入事实行失败：%s\n", err.c_str());
        return;
    }

    std::lock_guard<std::mutex> lock(mu_);
    written_rows_ += static_cast<std::int64_t>(metrics.size());
}

void PersistWorker::purge_once(std::int64_t now_ts) {
    /*
     * ========================================================================
     * 步骤1：删除早于保留窗口的行
     * ========================================================================
     * 保留窗口 = retain_days_ × 86400 秒
     */
    const std::int64_t before_ts =
        now_ts - static_cast<std::int64_t>(retain_days_) * 86400;
    const std::int64_t removed = db_.purge_metrics_before(before_ts);
    last_purge_ts_ = now_ts;

    {
        std::lock_guard<std::mutex> lock(mu_);
        purged_rows_ += removed;
    }

    std::fprintf(stderr, "[PerfStat][store] 保留清理：删除 %lld 行（保留 %d 天）\n",
                 static_cast<long long>(removed), retain_days_);
}

}  // namespace perfstat::store
