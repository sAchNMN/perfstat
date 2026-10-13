/*
 * ========================================================================
 * 模块：analyze / rule_db
 * ========================================================================
 * 作用：本地用途规则库 —— 三级瀑布的第 1 级（builtin 命中 + 人工覆盖）
 * 依据：
 *   docs/DESIGN.md 6.5（用途识别与清理建议）
 *   D13（builtin / manual 共存，manual 优先级最高）
 * 说明：
 *   - 识别对象键取「聚合实体」口径（见 usage_types.h 的 usage_key）
 *   - builtin 规则编译在 exe 内；manual 覆盖落 dim_usage 并在启动时载入内存
 *   - resolve 会被采集线程（序列化）与 HTTP 线程同时调用，故内部持锁；
 *     写路径把 DB IO 放在锁外，避免阻塞每轮序列化
 *   - attach 只在启动阶段调用一次，之后 db_ 不再变化
 */
#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "analyze/usage_types.h"
#include "store/store_types.h"

namespace perfstat::store {
class Database;
}

namespace perfstat::analyze {

class RuleDb {
public:
    RuleDb() = default;
    ~RuleDb() = default;

    RuleDb(const RuleDb&) = delete;
    RuleDb& operator=(const RuleDb&) = delete;

    /*
     * ====================================================================
     * 步骤：挂载数据库并载入覆盖
     * ====================================================================
     * 参数：db 为 nullptr 时表示纯内存模式（--no-persist 调试），
     *       此时人工覆盖仅在本进程会话内生效
     */
    bool attach(store::Database* db, std::string* err);

    /*
     * ====================================================================
     * 步骤：三级解析
     * ====================================================================
     * 优先级：manual > builtin > 未知（category=其他 / purpose=未知）
     * 线程安全：可在任意线程调用
     */
    UsageEntry resolve(const std::string& kind, const std::string& name,
                       const std::string& path) const;

    /*
     * ====================================================================
     * 步骤：人工覆盖
     * ====================================================================
     * 操作：
     *   1) 若已挂库：upsert 聚合实体 -> dim_usage（source='manual'）
     *   2) 更新内存覆盖表
     * 说明：manual 是最高优先级，会覆盖同键的 builtin 结果
     */
    bool set_manual(const std::string& kind, const std::string& name, const std::string& path,
                    const std::string& category, const std::string& purpose,
                    const std::string& suggestion, std::string* err);

    // 列出当前内存里的全部覆盖条目
    std::vector<store::UsageRow> list_overrides() const;

    /*
     * ====================================================================
     * 步骤：批量本地识别
     * ====================================================================
     * 返回：命中统计；未命中项收进 pending，前端可直接展示
     */
    AnalyzeResult analyze(const std::vector<EntityRef>& entities) const;

    std::size_t override_count() const;

private:
    // 无锁版本的 resolve，调用方必须已持 mu_
    UsageEntry resolve_locked(const std::string& kind, const std::string& name,
                              const std::string& path) const;

    mutable std::mutex mu_;
    store::Database*   db_{nullptr};
    // usage_key -> 覆盖条目
    std::unordered_map<std::string, store::UsageRow> overrides_;
};

}  // namespace perfstat::analyze
