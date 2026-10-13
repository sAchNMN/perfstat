/*
 * ========================================================================
 * 模块：api / serialize
 * ========================================================================
 * 作用：快照 JSON 序列化实现
 */
#include "api/serialize.h"

#include <utility>

#include "analyze/rule_db.h"
#include "analyze/usage_types.h"
#include "json.hpp"

namespace perfstat::api {
namespace {

/*
 * ========================================================================
 * 函数：usage_json
 * ========================================================================
 * 作用：把用途条目折成 JSON 对象
 * 说明：known=false 时前端按「未识别」渲染，category/purpose 为兜底值
 */
nlohmann::json usage_json(const analyze::UsageEntry& usage) {
    return nlohmann::json{
        {"category", usage.category},
        {"purpose", usage.purpose},
        {"suggestion", usage.suggestion},
        {"source", usage.source},
        {"known", usage.known},
    };
}

}  // namespace

std::string serialize_processes(const Snapshot& snapshot, const analyze::RuleDb* rules) {
    /*
     * ========================================================================
     * 步骤1：逐进程拼成 JSON 数组
     * ========================================================================
     * 口径：
     *   cpu    -> 占整机百分比
     *   mem_kb -> 工作集 KB
     *   io_*   -> 本轮增量 KB
     */
    nlohmann::json array = nlohmann::json::array();

    for (const ProcessSample& sample : snapshot.processes) {
        nlohmann::json item = {
            {"pid", sample.pid},
            {"ppid", sample.ppid},
            {"name", sample.name},
            {"path", sample.path},
            {"bg", sample.is_background},
            {"sys", sample.is_system_critical},
            {"ok", sample.accessible},
            {"cpu", sample.cpu_percent},
            {"mem_kb", sample.working_set_bytes / 1024},
            {"io_r_kb", sample.io_read_delta / 1024},
            {"io_w_kb", sample.io_write_delta / 1024},
        };

        /*
         * ====================================================================
         * 步骤2：附加用途（三级瀑布第 1 级：本地库）
         * ====================================================================
         * 说明：每轮重新解析，人工覆盖保存后下一帧即生效，无需重启
         */
        if (rules != nullptr) {
            item["uk"] = analyze::usage_key("process", sample.name, sample.path);
            item["usage"] = usage_json(rules->resolve("process", sample.name, sample.path));
        }

        array.push_back(std::move(item));
    }

    return array.dump();
}

std::string serialize_services(const ServiceSnapshot& snapshot, const analyze::RuleDb* rules) {
    /*
     * ========================================================================
     * 步骤1：逐服务拼成 JSON 数组
     * ========================================================================
     * 口径：
     *   occ      -> D18 三态：ok / denied / na
     *   cpu      -> 宿主占整机百分比（仅 occ=="ok" 有效）
     *   mem_kb   -> 宿主工作集 KB（仅 occ=="ok" 有效）
     *   shared   -> 同宿主共享服务数（含自身）
     */
    nlohmann::json array = nlohmann::json::array();

    for (const ServiceInfo& s : snapshot.services) {
        nlohmann::json item = {
            {"name", s.name},
            {"display", s.display_name},
            {"state", s.state},
            {"start_type", s.start_type},
            {"host_pid", s.host_pid},
            {"occ", s.occ_state},
            {"cpu", s.cpu_percent},
            {"mem_kb", s.working_set_bytes / 1024},
            {"shared", s.shared_count},
        };

        /*
         * ====================================================================
         * 步骤2：附加用途（服务键为服务名，无路径）
         * ====================================================================
         */
        if (rules != nullptr) {
            item["uk"] = analyze::usage_key("service", s.name, std::string());
            item["usage"] = usage_json(rules->resolve("service", s.name, std::string()));
        }

        array.push_back(std::move(item));
    }

    return array.dump();
}

}  // namespace perfstat::api
