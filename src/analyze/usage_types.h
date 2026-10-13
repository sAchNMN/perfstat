/*
 * ========================================================================
 * 模块：analyze / usage_types
 * ========================================================================
 * 作用：用途识别（三级瀑布）的公共数据模型
 * 依据：
 *   docs/DESIGN.md 6.5（用途识别与清理建议）
 *   docs/DESIGN.md 9（LLM 层）—— 本文件只定义契约，不含网络逻辑
 *   D13（规则库：builtin / llm / manual 三者共存）
 * 说明：
 *   - 识别对象的稳定键取「聚合实体」口径（进程 名+路径，服务 服务名），
 *     与 D12 的「聚合视图键」一致；用途是软件属性，不随实例启动时间漂移
 *   - 本层不认识 SQLite，也不认识 HTTP；持久化由 rule_db 注入的 store 负责
 */
#pragma once

#include <string>
#include <vector>

namespace perfstat::analyze {

/*
 * ========================================================================
 * 类型：UsageEntry
 * ========================================================================
 * 含义：单个实体的用途识别结果
 * source 取值：
 *   builtin —— 命中内置规则表
 *   manual  —— 人工在界面上覆盖过（最高优先级，LLM 回写不得覆盖）
 *   llm     —— LLM 自动回写（M6 接入后产生）
 *   none    —— 三级全部未命中，category/purpose 为兜底值
 */
struct UsageEntry {
    std::string category;      // 类别；未命中为 "其他"
    std::string purpose;       // 用途；未命中为 "未知"
    std::string suggestion;    // 清理建议；可为空
    std::string source{"none"};
    double      confidence{0.0};
    bool        known{false};  // 是否命中任一级

    UsageEntry() {
        category = "其他";
        purpose = "未知";
    }
};

/*
 * ========================================================================
 * 类型：EntityRef
 * ========================================================================
 * 含义：一个待识别实体的稳定标识
 */
struct EntityRef {
    std::string kind;  // 'process' | 'service'
    std::string name;  // 进程名（含扩展名）/ 服务名
    std::string path;  // exe 完整路径，服务为空
};

/*
 * ========================================================================
 * 类型：AnalyzeResult
 * ========================================================================
 * 含义：一次批量识别的统计结果
 * 说明：pending 为未命中清单，M6 的 LLM 层直接消费它
 */
struct AnalyzeResult {
    int total{0};
    int builtin_hits{0};
    int manual_hits{0};
    int llm_hits{0};
    int unknown{0};
    std::vector<EntityRef> pending;
};

// ASCII 小写化：Windows 进程名/服务名基本都是 ASCII，无需完整 Unicode 折叠
inline std::string to_lower_ascii(const std::string& in) {
    std::string out = in;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

/*
 * ========================================================================
 * 函数：usage_key
 * ========================================================================
 * 作用：把实体折成内存索引键
 * 口径：service -> "service|<小写服务名>"
 *       process -> "process|<小写进程名>|<小写路径>"
 */
inline std::string usage_key(const std::string& kind, const std::string& name,
                             const std::string& path) {
    if (kind == "service") {
        return "service|" + to_lower_ascii(name);
    }
    return "process|" + to_lower_ascii(name) + "|" + to_lower_ascii(path);
}

}  // namespace perfstat::analyze
