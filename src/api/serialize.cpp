/*
 * ========================================================================
 * 模块：api / serialize
 * ========================================================================
 * 作用：快照 JSON 序列化实现
 */
#include "api/serialize.h"

#include <unordered_map>
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

// ASCII 小写（路径 / 进程名比对用；Windows 路径不区分大小写）
std::string to_lower_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// 取路径里的文件名部分
std::string base_name(const std::string& path) {
    const std::size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
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

std::string serialize_startup(const StartupSnapshot& startup, const Snapshot& processes) {
    /*
     * ========================================================================
     * 步骤1：给当前进程快照建两张索引
     * ========================================================================
     * 说明：
     *   - by_path 优先（最准），退回 by_name 兜住「启动项写的是相对路径 /
     *     解析出来的是短路径」这类对不上的情况
     *   - 同名多实例只留第一个，占用取那个实例的值
     */
    std::unordered_map<std::string, const ProcessSample*> by_path;
    std::unordered_map<std::string, const ProcessSample*> by_name;
    for (const ProcessSample& p : processes.processes) {
        if (!p.path.empty()) {
            by_path.emplace(to_lower_ascii(p.path), &p);
        }
        if (!p.name.empty()) {
            by_name.emplace(to_lower_ascii(p.name), &p);
        }
    }

    /*
     * ========================================================================
     * 步骤2：逐项输出，就地关联占用
     * ========================================================================
     * 口径（沿 D17 的三态占用可见性）：
     *   occ = "ok"     命中且进程可读 -> cpu/mem 出值
     *         "denied" 命中但进程被拒（多为提权进程）-> cpu/mem 置 0，前端标「无权限」
     *         "na"     当前没在跑
     */
    nlohmann::json array = nlohmann::json::array();

    for (const StartupInfo& it : startup.items) {
        const ProcessSample* hit = nullptr;

        if (!it.exe_path.empty()) {
            const auto found = by_path.find(to_lower_ascii(it.exe_path));
            if (found != by_path.end()) {
                hit = found->second;
            }
        }
        if (hit == nullptr) {
            // 退化：用 exe 文件名 / 启动项名去比进程名
            std::string base = it.exe_path.empty() ? it.name : base_name(it.exe_path);
            if (base.find('.') == std::string::npos) {
                base += ".exe";  // 启动项名常常省了扩展名
            }
            const auto found = by_name.find(to_lower_ascii(base));
            if (found != by_name.end()) {
                hit = found->second;
            }
        }

        const bool readable = hit != nullptr && hit->accessible;
        const char* occ = (hit == nullptr) ? "na" : (readable ? "ok" : "denied");

        nlohmann::json item = {
            {"name", it.name},
            {"command", it.command},
            {"exe", it.exe_path},
            {"source", it.source},
            {"scope", it.scope},
            {"kind", it.kind},
            {"enabled", it.enabled},
            {"pid", hit != nullptr ? hit->pid : 0},
            {"running", hit != nullptr},
            {"occ", occ},
            {"cpu", readable ? hit->cpu_percent : 0.0},
            {"mem_kb", readable ? hit->working_set_bytes / 1024 : 0},
        };
        array.push_back(std::move(item));
    }

    return array.dump();
}

}  // namespace perfstat::api
