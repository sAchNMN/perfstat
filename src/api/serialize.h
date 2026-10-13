/*
 * ========================================================================
 * 模块：api / serialize
 * ========================================================================
 * 作用：把采集快照转成前端可用的 JSON
 * 依据：docs/DESIGN.md 5（SSE 帧格式）
 * 说明：/api/processes 与 /api/realtime 共用同一序列化，保证口径一致
 */
#pragma once

#include <string>

#include "api/snapshot_store.h"

namespace perfstat::api {

// 进程数组 -> JSON 字符串；字段：pid/name/path/bg/sys/ok/cpu/mem_kb/io_*_kb
std::string serialize_processes(const Snapshot& snapshot);

}  // namespace perfstat::api
