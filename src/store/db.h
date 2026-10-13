/*
 * ========================================================================
 * 模块：store / db
 * ========================================================================
 * 作用：SQLite 连接与 schema 管理，提供维度 upsert、事实批量写入、保留清理
 * 依据：docs/DESIGN.md 7（数据模型）/ 6.4（存储与聚合）
 * 说明：
 *   - 开 WAL + synchronous=NORMAL，满足轻量化与并发读要求
 *   - 本类是纯数据访问层，不含线程逻辑；线程模型见 persist_worker
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "store/store_types.h"

struct sqlite3;

namespace perfstat::store {

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /*
     * ====================================================================
     * 步骤：打开并初始化
     * ====================================================================
     * 操作：打开（无则建）-> 设置 PRAGMA -> 建表
     */
    bool open(const std::string& path, std::string* err);
    void close();
    bool is_open() const { return db_ != nullptr; }

    // 实体 upsert：不存在则插入，存在则刷新 last_seen/name/path，返回 entity_id
    std::int64_t upsert_entity(const EntityRow& entity, std::int64_t now_ts, std::string* err);

    // 单事务批量写入分钟聚合行（同键覆盖）
    bool insert_metric_rows(const std::vector<MetricRow>& rows, std::string* err);

    // 删除 bucket_ts < before_ts 的行，返回删除行数
    std::int64_t purge_metrics_before(std::int64_t before_ts);

    // 概况统计，用于验收与前端展示
    struct Stats {
        std::int64_t entity_count{0};
        std::int64_t metric_rows{0};
        std::int64_t oldest_bucket{0};
        std::int64_t newest_bucket{0};
    };
    bool stats(Stats* out, std::string* err);

private:
    bool ensure_schema(std::string* err);

    sqlite3* db_{nullptr};
};

}  // namespace perfstat::store
