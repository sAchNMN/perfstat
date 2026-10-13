/*
 * ========================================================================
 * 模块：collect / service_collector
 * ========================================================================
 * 作用：SCM 枚举全部 Win32 服务，关联进程快照产出三态占用
 * 依据：docs/DESIGN.md 6.2（服务采集）/ D11（共享宿主）/ D18（占用可见性）
 * 说明：
 *   - 服务元数据来自 SCM，不碰进程句柄，零提权依赖
 *   - 占用来自当前轮进程快照：宿主 PID 在快照中 accessible 为真才出值
 *   - 启动类型经 QueryServiceConfig 取得，周期刷新避免每轮数百 RPC
 */
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>

#include "collect/process_metrics.h"

namespace perfstat {

/*
 * ========================================================================
 * 类型：ServiceInfo
 * ========================================================================
 * 含义：单个服务在一轮采样中的快照
 * 字段：
 *   - name / display_name / state / start_type：SCM 元数据（永远可得）
 *   - host_pid / has_host：宿主进程 PID（未运行则为 0）
 *   - occ_state：D18 三态 — "ok"(有值) / "denied"(宿主 PID 被拒) / "na"(不适用)
 *   - cpu_percent / working_set_bytes：宿主真实占用（仅 occ_state=="ok" 有效）
 *   - shared_count：同宿主共享的服务数（含自身，仅共享宿主 >1）
 */
struct ServiceInfo {
    std::string   name;            // 服务名（键）
    std::string   display_name;    // 显示名
    std::string   state;           // running / stopped / start_pending / ...
    std::string   start_type;      // auto / manual / disabled / delayed / unknown
    std::uint32_t host_pid{0};     // 宿主进程 PID，0 表示无宿主（未运行）
    bool          has_host{false};

    std::string   occ_state{"na"}; // D18：ok / denied / na
    double        cpu_percent{0.0};
    std::uint64_t working_set_bytes{0};
    int           shared_count{0};
};

using ServiceInfoList = std::vector<ServiceInfo>;

class ServiceCollector {
public:
    ServiceCollector();
    ~ServiceCollector();

    /*
     * ====================================================================
     * 步骤：采集一轮
     * ====================================================================
     * 参数：processes 为本轮进程快照，用于宿主占用关联
     * 返回：全部服务快照（含三态占用）
     */
    ServiceInfoList collect_round(const ProcessSampleList& processes, std::uint64_t round);

private:
    // 每轮枚举 SCM，返回基础元数据（不含启动类型）
    std::vector<ServiceInfo> enumerate();

    // 查询单个服务启动类型（OpenService + QueryServiceConfig）
    std::string query_start_type(const std::string& service_name);

    SC_HANDLE scm_{nullptr};

    // 启动类型缓存：服务名 -> 启动类型；周期刷新
    std::unordered_map<std::string, std::string> start_type_cache_;
    std::uint64_t last_config_refresh_{0};
    static constexpr std::uint64_t kConfigRefreshRounds = 15;  // 约 30s（2s 一轮）
};

}  // namespace perfstat
