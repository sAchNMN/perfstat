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

/*
 * ========================================================================
 * 类型：UsageRow
 * ========================================================================
 * 对应表：dim_usage（用途 / 建议库），见 D13
 * 说明：
 *   - kind / name / path 来自联表 dim_entity，**读取时填充，写入时忽略**
 *   - entity_id 指向「聚合实体」（进程 name|path，服务 服务名），
 *     而非采集落库的实例实体（进程名|路径|启动时间），两者 instance_key 段数不同，不会撞键
 *   - source 两级：builtin（内置）| manual（人工覆盖）
 *     manual 的记录不得被 lower 优先级覆盖，保护逻辑在 Database::upsert_usage
 */
struct UsageRow {
    std::int64_t entity_id{0};
    std::string  kind;          // 'process' | 'service'（联表字段）
    std::string  name;          // 进程名 / 服务名（联表字段）
    std::string  path;          // exe 完整路径（联表字段）
    std::string  category;      // 类别
    std::string  purpose;       // 用途
    std::string  suggestion;    // 清理建议
    std::string  source;        // builtin | manual
    double       confidence{0.0};
    std::int64_t updated_at{0};
};

// 聚合实体的 instance_key：进程 name|path，服务 服务名
inline std::string aggregate_instance_key(const std::string& kind, const std::string& name,
                                          const std::string& path) {
    if (kind == "service") {
        return name;
    }
    return name + "|" + path;
}

/*
 * ========================================================================
 * 类型：EntityStatRow
 * ========================================================================
 * 含义：一段区间内单个实体的汇总（/api/stats 的汇总榜行）
 * 依据：DESIGN D7（统计口径）
 * 说明：
 *   - cpu_avg 为区间内各分钟均值的平均，cpu_max 为区间内峰值
 *   - mem_avg_kb / mem_max_kb 同口径
 *   - io_*_kb 为区间内各分钟增量之和
 */
struct EntityStatRow {
    std::int64_t entity_id{0};
    std::string  kind;          // 'process' | 'service'
    std::string  name;
    std::string  path;
    double       cpu_avg{0.0};
    double       cpu_max{0.0};
    std::int64_t mem_avg_kb{0};
    std::int64_t mem_max_kb{0};
    std::int64_t io_read_kb{0};
    std::int64_t io_write_kb{0};
    std::int64_t samples{0};    // 区间内采样点总数
    std::int64_t buckets{0};    // 区间内有数据的分钟数
};

/*
 * ========================================================================
 * 类型：SeriesPoint
 * ========================================================================
 * 含义：时间序列上的一个点（趋势曲线用）
 * 说明：
 *   - 全系统序列：cpu_avg 为同桶内各实体 cpu_avg 之和（即整机占用），mem 同理
 *   - 单实体序列：即该实体在该桶内的原始聚合值
 */
struct SeriesPoint {
    std::int64_t bucket_ts{0};
    double       cpu_avg{0.0};
    double       cpu_max{0.0};
    std::int64_t mem_avg_kb{0};
    std::int64_t mem_max_kb{0};
    std::int64_t io_read_kb{0};
    std::int64_t io_write_kb{0};
};

}  // namespace perfstat::store
