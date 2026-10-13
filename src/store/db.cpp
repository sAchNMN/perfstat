/*
 * ========================================================================
 * 模块：store / db
 * ========================================================================
 * 作用：SQLite 数据访问实现
 * 依据：docs/DESIGN.md 7 / 6.4
 * 说明：全程用预编译语句 + 绑定参数，杜绝 SQL 拼接
 */
#include "store/db.h"

#include <sqlite3.h>

#include <string>

namespace perfstat::store {
namespace {

// 执行无结果集 SQL；失败时把 sqlite 的报错写进 err
bool exec_sql(sqlite3* db, const char* sql, std::string* err) {
    char* msg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);
    if (rc != SQLITE_OK) {
        if (err != nullptr) {
            *err = std::string("SQL 失败：") + (msg != nullptr ? msg : sqlite3_errstr(rc));
        }
        sqlite3_free(msg);
        return false;
    }
    return true;
}

// 绑定文本；SQLITE_TRANSIENT 让 SQLite 自行复制，调用方无需维系生命周期
void bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

}  // namespace

Database::~Database() { close(); }

bool Database::open(const std::string& path, std::string* err) {
    if (db_ != nullptr) {
        return true;
    }

    /*
     * ========================================================================
     * 步骤1：打开数据库（无则建）
     * ========================================================================
     * 说明：FULLMUTEX 让连接自身串行化，配合 SQLITE_THREADSAFE=1
     */
    const int rc = sqlite3_open_v2(
        path.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        if (err != nullptr) {
            *err = std::string("打开数据库失败：") + sqlite3_errmsg(db_);
        }
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    /*
     * ========================================================================
     * 步骤2：设置 PRAGMA
     * ========================================================================
     * WAL          -> 读写并发，写不阻塞读
     * synchronous  -> NORMAL，WAL 下已足够安全且更快
     * busy_timeout -> 被占用时等待而非立即失败
     */
    sqlite3_busy_timeout(db_, 5000);
    if (!exec_sql(db_, "PRAGMA journal_mode=WAL;", err)) return false;
    if (!exec_sql(db_, "PRAGMA synchronous=NORMAL;", err)) return false;
    if (!exec_sql(db_, "PRAGMA foreign_keys=ON;", err)) return false;

    /*
     * ========================================================================
     * 步骤3：建表
     * ========================================================================
     */
    return ensure_schema(err);
}

void Database::close() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::ensure_schema(std::string* err) {
    /*
     * ========================================================================
     * 步骤1：实体维表
     * ========================================================================
     * UNIQUE(kind, instance_key) 是 upsert 的冲突键
     */
    static const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS dim_entity (
    entity_id    INTEGER PRIMARY KEY,
    kind         TEXT    NOT NULL,
    name         TEXT    NOT NULL,
    path         TEXT,
    publisher    TEXT,
    instance_key TEXT    NOT NULL,
    first_seen   INTEGER NOT NULL,
    last_seen    INTEGER NOT NULL,
    UNIQUE(kind, instance_key)
);

CREATE TABLE IF NOT EXISTS dim_usage (
    entity_id  INTEGER PRIMARY KEY REFERENCES dim_entity(entity_id),
    category   TEXT,
    purpose    TEXT,
    suggestion TEXT,
    source     TEXT    NOT NULL,
    confidence REAL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS fact_metric_1m (
    entity_id    INTEGER NOT NULL,
    bucket_ts    INTEGER NOT NULL,
    cpu_avg      REAL,
    cpu_max      REAL,
    mem_avg_kb   INTEGER,
    mem_max_kb   INTEGER,
    io_read_kb   INTEGER,
    io_write_kb  INTEGER,
    sample_count INTEGER,
    PRIMARY KEY (entity_id, bucket_ts)
);

CREATE INDEX IF NOT EXISTS idx_fact_ts ON fact_metric_1m(bucket_ts);

CREATE TABLE IF NOT EXISTS dim_service_host (
    service_name   TEXT PRIMARY KEY,
    display_name   TEXT,
    host_entity_id INTEGER REFERENCES dim_entity(entity_id),
    start_type     TEXT,
    state          TEXT
);
)SQL";

    return exec_sql(db_, kSchema, err);
}

std::int64_t Database::upsert_entity(const EntityRow& entity, std::int64_t now_ts, std::string* err) {
    if (db_ == nullptr) {
        if (err != nullptr) *err = "数据库未打开";
        return 0;
    }

    /*
     * ========================================================================
     * 步骤1：按 (kind, instance_key) 查已有实体
     * ========================================================================
     */
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT entity_id FROM dim_entity WHERE kind=?1 AND instance_key=?2",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        if (err != nullptr) *err = sqlite3_errmsg(db_);
        return 0;
    }
    bind_text(stmt, 1, entity.kind);
    bind_text(stmt, 2, entity.instance_key);

    std::int64_t entity_id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        entity_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);

    /*
     * ========================================================================
     * 步骤2：命中则刷新 last_seen，未命中则插入新行
     * ========================================================================
     */
    if (entity_id != 0) {
        sqlite3_stmt* upd = nullptr;
        if (sqlite3_prepare_v2(
                db_, "UPDATE dim_entity SET last_seen=?1, name=?2, path=?3 WHERE entity_id=?4", -1,
                &upd, nullptr) != SQLITE_OK) {
            if (err != nullptr) *err = sqlite3_errmsg(db_);
            return 0;
        }
        sqlite3_bind_int64(upd, 1, now_ts);
        bind_text(upd, 2, entity.name);
        bind_text(upd, 3, entity.path);
        sqlite3_bind_int64(upd, 4, entity_id);
        const int rc = sqlite3_step(upd);
        sqlite3_finalize(upd);
        if (rc != SQLITE_DONE) {
            if (err != nullptr) *err = sqlite3_errmsg(db_);
            return 0;
        }
        return entity_id;
    }

    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "INSERT INTO dim_entity"
                           "(kind,name,path,instance_key,first_seen,last_seen)"
                           " VALUES(?1,?2,?3,?4,?5,?5)",
                           -1, &ins, nullptr) != SQLITE_OK) {
        if (err != nullptr) *err = sqlite3_errmsg(db_);
        return 0;
    }
    bind_text(ins, 1, entity.kind);
    bind_text(ins, 2, entity.name);
    bind_text(ins, 3, entity.path);
    bind_text(ins, 4, entity.instance_key);
    sqlite3_bind_int64(ins, 5, now_ts);
    const int rc = sqlite3_step(ins);
    sqlite3_finalize(ins);
    if (rc != SQLITE_DONE) {
        if (err != nullptr) *err = sqlite3_errmsg(db_);
        return 0;
    }
    return sqlite3_last_insert_rowid(db_);
}

bool Database::insert_metric_rows(const std::vector<MetricRow>& rows, std::string* err) {
    if (db_ == nullptr) {
        if (err != nullptr) *err = "数据库未打开";
        return false;
    }
    if (rows.empty()) {
        return true;
    }

    /*
     * ========================================================================
     * 步骤1：开事务
     * ========================================================================
     * 说明：整批共用一个事务，避免逐行 fsync
     */
    if (!exec_sql(db_, "BEGIN IMMEDIATE;", err)) {
        return false;
    }

    /*
     * ========================================================================
     * 步骤2：预编译一条语句，循环绑定写入
     * ========================================================================
     * 冲突策略：同 (entity_id, bucket_ts) 覆盖，支持重复 flush 幂等
     */
    sqlite3_stmt* stmt = nullptr;
    static const char* kInsert =
        "INSERT INTO fact_metric_1m"
        "(entity_id,bucket_ts,cpu_avg,cpu_max,mem_avg_kb,mem_max_kb,"
        " io_read_kb,io_write_kb,sample_count)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)"
        " ON CONFLICT(entity_id,bucket_ts) DO UPDATE SET"
        " cpu_avg=excluded.cpu_avg, cpu_max=excluded.cpu_max,"
        " mem_avg_kb=excluded.mem_avg_kb, mem_max_kb=excluded.mem_max_kb,"
        " io_read_kb=excluded.io_read_kb, io_write_kb=excluded.io_write_kb,"
        " sample_count=excluded.sample_count";
    if (sqlite3_prepare_v2(db_, kInsert, -1, &stmt, nullptr) != SQLITE_OK) {
        if (err != nullptr) *err = sqlite3_errmsg(db_);
        exec_sql(db_, "ROLLBACK;", nullptr);
        return false;
    }

    bool ok = true;
    for (const MetricRow& row : rows) {
        sqlite3_bind_int64(stmt, 1, row.entity_id);
        sqlite3_bind_int64(stmt, 2, row.bucket_ts);
        sqlite3_bind_double(stmt, 3, row.cpu_avg);
        sqlite3_bind_double(stmt, 4, row.cpu_max);
        sqlite3_bind_int64(stmt, 5, row.mem_avg_kb);
        sqlite3_bind_int64(stmt, 6, row.mem_max_kb);
        sqlite3_bind_int64(stmt, 7, row.io_read_kb);
        sqlite3_bind_int64(stmt, 8, row.io_write_kb);
        sqlite3_bind_int(stmt, 9, row.sample_count);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            ok = false;
            if (err != nullptr) *err = sqlite3_errmsg(db_);
            break;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);

    /*
     * ========================================================================
     * 步骤3：按结果提交或回滚
     * ========================================================================
     */
    if (!ok) {
        exec_sql(db_, "ROLLBACK;", nullptr);
        return false;
    }
    return exec_sql(db_, "COMMIT;", err);
}

std::int64_t Database::purge_metrics_before(std::int64_t before_ts) {
    /*
     * ========================================================================
     * 步骤1：删除过期事实行
     * ========================================================================
     * 说明：走 idx_fact_ts 索引，一次删干净
     */
    if (db_ == nullptr) {
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM fact_metric_1m WHERE bucket_ts < ?1", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int64(stmt, 1, before_ts);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return 0;
    }
    return sqlite3_changes(db_);
}

bool Database::stats(Stats* out, std::string* err) {
    if (db_ == nullptr || out == nullptr) {
        if (err != nullptr) *err = "数据库未打开";
        return false;
    }

    /*
     * ========================================================================
     * 步骤1：一次查询取四组聚合值
     * ========================================================================
     */
    sqlite3_stmt* stmt = nullptr;
    static const char* kSql =
        "SELECT (SELECT COUNT(*) FROM dim_entity),"
        "       (SELECT COUNT(*) FROM fact_metric_1m),"
        "       (SELECT COALESCE(MIN(bucket_ts),0) FROM fact_metric_1m),"
        "       (SELECT COALESCE(MAX(bucket_ts),0) FROM fact_metric_1m)";
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (err != nullptr) *err = sqlite3_errmsg(db_);
        return false;
    }

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->entity_count = sqlite3_column_int64(stmt, 0);
        out->metric_rows = sqlite3_column_int64(stmt, 1);
        out->oldest_bucket = sqlite3_column_int64(stmt, 2);
        out->newest_bucket = sqlite3_column_int64(stmt, 3);
        ok = true;
    } else if (err != nullptr) {
        *err = sqlite3_errmsg(db_);
    }
    sqlite3_finalize(stmt);
    return ok;
}

}  // namespace perfstat::store
