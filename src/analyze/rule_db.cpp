/*
 * ========================================================================
 * 模块：analyze / rule_db
 * ========================================================================
 * 作用：本地规则库实现
 * 依据：docs/DESIGN.md 6.5 / D13
 */
#include "analyze/rule_db.h"

#include <cstdint>
#include <ctime>
#include <utility>

#include "analyze/builtin_rules.h"
#include "store/db.h"

namespace perfstat::analyze {

bool RuleDb::attach(store::Database* db, std::string* err) {
    /*
     * ========================================================================
     * 步骤1：记录数据库句柄
     * ========================================================================
     * 说明：nullptr 表示纯内存模式，人工覆盖只在本会话生效
     */
    db_ = db;

    /*
     * ========================================================================
     * 步骤2：清空旧覆盖
     * ========================================================================
     */
    {
        std::lock_guard<std::mutex> lock(mu_);
        overrides_.clear();
    }
    if (db_ == nullptr) {
        return true;
    }

    /*
     * ========================================================================
     * 步骤3：从 dim_usage 载入已有覆盖
     * ========================================================================
     * 说明：联表拿到的 name/path 用于重建内存索引键
     */
    std::vector<store::UsageRow> rows;
    if (!db_->load_usages(&rows, err)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mu_);
    overrides_.reserve(rows.size());
    for (store::UsageRow& row : rows) {
        const std::string key = usage_key(row.kind, row.name, row.path);
        overrides_[key] = std::move(row);
    }
    return true;
}

UsageEntry RuleDb::resolve(const std::string& kind, const std::string& name,
                           const std::string& path) const {
    std::lock_guard<std::mutex> lock(mu_);
    return resolve_locked(kind, name, path);
}

UsageEntry RuleDb::resolve_locked(const std::string& kind, const std::string& name,
                                  const std::string& path) const {
    UsageEntry entry;

    /*
     * ========================================================================
     * 步骤1：覆盖优先（人工覆盖）
     * ========================================================================
     * 说明：同键只保留一条，人工覆盖直接覆盖内置结果
     */
    const auto it = overrides_.find(usage_key(kind, name, path));
    if (it != overrides_.end()) {
        entry.category = it->second.category;
        entry.purpose = it->second.purpose;
        entry.suggestion = it->second.suggestion;
        entry.source = it->second.source;
        entry.confidence = it->second.confidence;
        entry.known = true;
        return entry;
    }

    /*
     * ========================================================================
     * 步骤2：内置规则表
     * ========================================================================
     * 说明：进程与服务各查各表，避免同名键互相误命中
     */
    const BuiltinRule* rule = nullptr;
    if (kind == "service") {
        rule = find_builtin_rule(builtin_service_rules(), builtin_service_rule_count(), name);
    } else {
        rule = find_builtin_rule(builtin_process_rules(), builtin_process_rule_count(), name);
    }
    if (rule != nullptr) {
        entry.category = rule->category;
        entry.purpose = rule->purpose;
        entry.suggestion = rule->suggestion;
        entry.source = "builtin";
        entry.confidence = 0.9;
        entry.known = true;
        return entry;
    }

    /*
     * ========================================================================
     * 步骤3：兜底
     * ========================================================================
     * 说明：保持 UsageEntry 默认值 —— category=其他 / purpose=未知 / source=none
     */
    return entry;
}

bool RuleDb::set_manual(const std::string& kind, const std::string& name,
                        const std::string& path, const std::string& category,
                        const std::string& purpose, const std::string& suggestion,
                        std::string* err) {
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    std::int64_t entity_id = 0;

    /*
     * ========================================================================
     * 步骤1：持久化到 dim_usage（锁外执行，避免 DB IO 阻塞采集线程的序列化）
     * ========================================================================
     * 操作：
     *   1) upsert 聚合实体（进程 name|path / 服务 服务名）
     *   2) upsert dim_usage，source='manual'
     */
    if (db_ != nullptr) {
        store::EntityRow entity;
        entity.kind = kind;
        entity.name = name;
        entity.path = path;
        entity.instance_key = store::aggregate_instance_key(kind, name, path);

        entity_id = db_->upsert_entity(entity, now, err);
        if (entity_id == 0) {
            return false;
        }

        store::UsageRow usage;
        usage.entity_id = entity_id;
        usage.category = category;
        usage.purpose = purpose;
        usage.suggestion = suggestion;
        usage.source = "manual";
        usage.confidence = 1.0;
        usage.updated_at = now;
        if (!db_->upsert_usage(usage, err)) {
            return false;
        }
    }

    /*
     * ========================================================================
     * 步骤2：更新内存覆盖表
     * ========================================================================
     * 说明：同时填联表字段，使 list_overrides 无需回查数据库
     */
    store::UsageRow row;
    row.entity_id = entity_id;
    row.kind = kind;
    row.name = name;
    row.path = path;
    row.category = category;
    row.purpose = purpose;
    row.suggestion = suggestion;
    row.source = "manual";
    row.confidence = 1.0;
    row.updated_at = now;

    {
        std::lock_guard<std::mutex> lock(mu_);
        overrides_[usage_key(kind, name, path)] = std::move(row);
    }
    return true;
}

std::vector<store::UsageRow> RuleDb::list_overrides() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<store::UsageRow> out;
    out.reserve(overrides_.size());
    for (const auto& kv : overrides_) {
        out.push_back(kv.second);
    }
    return out;
}

AnalyzeResult RuleDb::analyze(const std::vector<EntityRef>& entities) const {
    AnalyzeResult result;
    result.total = static_cast<int>(entities.size());

    /*
     * ========================================================================
     * 步骤1：逐实体解析并分类计数
     * ========================================================================
     * 说明：整个过程只读内存，无网络、无磁盘 —— 这是 D13「零成本命中」的落点
     */
    std::lock_guard<std::mutex> lock(mu_);
    for (const EntityRef& entity : entities) {
        const UsageEntry entry = resolve_locked(entity.kind, entity.name, entity.path);
        if (!entry.known) {
            ++result.unknown;
            result.pending.push_back(entity);
        } else if (entry.source == "manual") {
            ++result.manual_hits;
        } else {
            ++result.builtin_hits;
        }
    }
    return result;
}

std::size_t RuleDb::override_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return overrides_.size();
}

}  // namespace perfstat::analyze
