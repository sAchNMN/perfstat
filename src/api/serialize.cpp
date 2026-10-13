/*
 * ========================================================================
 * 模块：api / serialize
 * ========================================================================
 * 作用：快照 JSON 序列化实现
 */
#include "api/serialize.h"

#include "json.hpp"

namespace perfstat::api {

std::string serialize_processes(const Snapshot& snapshot) {
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
        array.push_back(nlohmann::json{
            {"pid", sample.pid},
            {"name", sample.name},
            {"path", sample.path},
            {"bg", sample.is_background},
            {"sys", sample.is_system_critical},
            {"ok", sample.accessible},
            {"cpu", sample.cpu_percent},
            {"mem_kb", sample.working_set_bytes / 1024},
            {"io_r_kb", sample.io_read_delta / 1024},
            {"io_w_kb", sample.io_write_delta / 1024},
        });
    }

    return array.dump();
}

}  // namespace perfstat::api
