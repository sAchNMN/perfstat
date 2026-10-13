/*
 * ========================================================================
 * 模块：api / serialize
 * ========================================================================
 * 作用：把采集快照转成前端可用的 JSON
 * 依据：docs/DESIGN.md 5（SSE 帧格式）/ 6.5（用途识别）
 * 说明：
 *   - /api/processes 与 /api/realtime 共用同一序列化，保证口径一致
 *   - 用途字段（usage）在此就地解析并附带，前端无需二次请求
 *     —— D13 命中走内存查表，300 实体一轮成本可忽略
 *   - uk 是实体的用途索引键（聚合口径），人工覆盖时原样回传即可
 */
#pragma once

#include <string>

#include "api/service_store.h"
#include "api/snapshot_store.h"
#include "api/startup_store.h"

namespace perfstat::analyze {
class RuleDb;
}

namespace perfstat::api {

/*
 * 进程数组 -> JSON 字符串
 * 字段：pid/name/path/bg/sys/ok/cpu/mem_kb/io_*_kb/uk/usage
 * 说明：rules 可为 nullptr，此时省略 uk/usage
 */
std::string serialize_processes(const Snapshot& snapshot, const analyze::RuleDb* rules);

/*
 * 服务数组 -> JSON 字符串
 * 字段：name/display/state/start_type/host_pid/occ/cpu/mem_kb/shared/uk/usage
 */
std::string serialize_services(const ServiceSnapshot& snapshot, const analyze::RuleDb* rules);

/*
 * 启动项数组 -> JSON 字符串
 * 字段：name/command/exe/source/scope/kind/enabled/pid/running/cpu/mem_kb
 * 说明：清单本身是静态的（startup 快照），占用则用「当前进程快照」现算 ——
 *       这样清单不必频繁重读注册表，占用又始终是最新一轮的值
 */
std::string serialize_startup(const StartupSnapshot& startup, const Snapshot& processes);

}  // namespace perfstat::api
