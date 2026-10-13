/*
 * ========================================================================
 * 模块：store / store_types
 * ========================================================================
 * 作用：定义存储层与聚合层共用的行结构
 * 依据：docs/DESIGN.md 7（数据模型）
 * 说明：
 *   - 与 SQLite 表结构一一对应，不含任何 sqlite3 类型，便于上层复用
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace perfstat::store {

/*
 * ========================================================================
 * 类型：EntityRow
 * ========================================================================
 * 对应表：dim_entity
 * 说明：instance_key 是 D12 定下的稳定标识
 *   - 进程：name|path|start_time_100ns
 *   - 服务：服务名
 */
struct EntityRow {
    std::string kind;          // 'process' | 'service'
    std::string name;          // 进程名 / 服务名
    std::string path;          // exe 完整路径，服务可为空
    std::string instance_key;  // 唯一键（与 kind 组合）
};

/*
 * ========================================================================
 * 类型：MetricRow
 * ========================================================================
 * 对应表：fact_metric_1m（一进程一分钟一行）
 * 口径：见 DESIGN D7
 *   - cpu_avg / cpu_max 为占整机百分比
 *   - mem_*_kb 为工作集
 *   - io_*_kb 为该分钟的增量累加
 */
struct MetricRow {
    std::int64_t entity_id{0};
    std::int64_t bucket_ts{0};   // 分钟对齐的 unix 时间戳
    double       cpu_avg{0.0};
    double       cpu_max{0.0};
    std::int64_t mem_avg_kb{0};
    std::int64_t mem_max_kb{0};
    std::int64_t io_read_kb{0};
    std::int64_t io_write_kb{0};
    int          sample_count{0};
};

/*
 * ========================================================================
 * 类型：PendingRow
 * ========================================================================
 * 含义：待落库的一行 = 实体信息 + 指标
 * 说明：metric.entity_id 由写库线程 upsert 实体后回填，聚合层不接触数据库
 */
struct PendingRow {
    EntityRow entity;
    MetricRow metric;
};

/*
 * ========================================================================
 * 类型：MinuteBucket
 * ========================================================================
 * 含义：一个已完成分钟的全部行，作为写库线程的消费单元
 */
struct MinuteBucket {
    std::int64_t            bucket_ts{0};
    std::vector<PendingRow> rows;
};

}  // namespace perfstat::store
