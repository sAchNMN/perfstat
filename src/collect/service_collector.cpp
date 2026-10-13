/*
 * ========================================================================
 * 模块：collect / service_collector
 * ========================================================================
 * 作用：SCM 枚举 + 进程快照关联，产出三态占用服务列表
 * 依据：docs/DESIGN.md 6.2 / D11 / D18
 */
#include "collect/service_collector.h"

#include <cstdio>

namespace perfstat {
namespace {

// 宽字符转 UTF-8
std::string wide_to_utf8(const wchar_t* w) {
    if (w == nullptr || *w == L'\0') {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

// UTF-8 转宽字符
std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

// SERVICE_STATE -> 字符串
std::string state_to_string(DWORD state) {
    switch (state) {
        case SERVICE_RUNNING:       return "running";
        case SERVICE_STOPPED:       return "stopped";
        case SERVICE_START_PENDING: return "start_pending";
        case SERVICE_STOP_PENDING:  return "stop_pending";
        case SERVICE_PAUSE_PENDING: return "pause_pending";
        case SERVICE_PAUSED:        return "paused";
        default:                    return "unknown";
    }
}

}  // namespace

ServiceCollector::ServiceCollector() {
    /*
     * ========================================================================
     * 步骤1：打开 SCM 句柄（仅枚举权限，不碰进程）
     * ========================================================================
     */
    scm_ = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
    if (scm_ == nullptr) {
        std::fprintf(stderr, "[PerfStat][svc] OpenSCManager 失败，err=%lu\n", GetLastError());
    }
}

ServiceCollector::~ServiceCollector() {
    if (scm_ != nullptr) {
        CloseServiceHandle(scm_);
    }
}

std::vector<ServiceInfo> ServiceCollector::enumerate() {
    /*
     * ========================================================================
     * 步骤1：EnumServicesStatusEx 枚举全部 Win32 服务
     * ========================================================================
     * 数据源：SCM（零提权依赖）
     * 操作：
     *   1) 缓冲不足时按返回的需求扩容重试
     *   2) 取服务名 / 显示名 / 状态 / 宿主 PID
     */
    std::vector<ServiceInfo> out;
    if (scm_ == nullptr) {
        return out;
    }

    DWORD buf_size = 64 * 1024;
    std::vector<BYTE> buf(buf_size);
    DWORD bytes_needed = 0;
    DWORD returned = 0;
    DWORD resume = 0;

    for (;;) {
        const BOOL ok = EnumServicesStatusExW(
            scm_, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buf.data(), buf_size,
            &bytes_needed, &returned, &resume, nullptr);
        if (ok) {
            break;
        }
        if (GetLastError() == ERROR_MORE_DATA) {
            buf_size = bytes_needed + 4096;
            buf.resize(buf_size);
            continue;
        }
        std::fprintf(stderr, "[PerfStat][svc] EnumServicesStatusEx 失败，err=%lu\n",
                     GetLastError());
        return out;
    }

    auto* arr = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
    out.reserve(returned);
    for (DWORD i = 0; i < returned; ++i) {
        const ENUM_SERVICE_STATUS_PROCESSW& s = arr[i];
        ServiceInfo info;
        info.name = wide_to_utf8(s.lpServiceName);
        info.display_name = wide_to_utf8(s.lpDisplayName);
        info.state = state_to_string(s.ServiceStatusProcess.dwCurrentState);
        info.host_pid = s.ServiceStatusProcess.dwProcessId;
        info.has_host = (info.host_pid != 0);
        out.push_back(std::move(info));
    }
    return out;
}

std::string ServiceCollector::query_start_type(const std::string& service_name) {
    /*
     * ========================================================================
     * 步骤1：打开服务并查询配置
     * ========================================================================
     * 接口：OpenServiceW + QueryServiceConfigW
     * 注意：部分服务配置读取会被拒（ACCESS_DENIED），此时返回空串
     */
    if (scm_ == nullptr) {
        return {};
    }
    const std::wstring wide = utf8_to_wide(service_name);
    SC_HANDLE svc = OpenServiceW(scm_, wide.c_str(), SERVICE_QUERY_CONFIG);
    if (svc == nullptr) {
        return {};
    }

    std::string result;
    std::vector<BYTE> buf(4096);
    DWORD needed = 0;
    BOOL ok = QueryServiceConfigW(reinterpret_cast<SC_HANDLE>(svc),
                                  reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data()),
                                  static_cast<DWORD>(buf.size()), &needed);
    if (!ok && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        buf.resize(needed);
        ok = QueryServiceConfigW(svc, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data()),
                                 static_cast<DWORD>(buf.size()), &needed);
    }
    if (ok) {
        const auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
        switch (cfg->dwStartType) {
            case SERVICE_AUTO_START:   result = "auto";    break;
            case SERVICE_DEMAND_START: result = "manual";  break;
            case SERVICE_DISABLED:     result = "disabled"; break;
            default:                   result = "unknown";  break;
        }
    }
    CloseServiceHandle(svc);
    return result;
}

ServiceInfoList ServiceCollector::collect_round(const ProcessSampleList& processes,
                                               std::uint64_t round) {
    /*
     * ========================================================================
     * 步骤1：枚举本轮 SCM 元数据
     * ========================================================================
     */
    std::vector<ServiceInfo> base = enumerate();

    /*
     * ========================================================================
     * 步骤2：补充启动类型（周期刷新缓存）
     * ========================================================================
     * 策略：首轮或距上次刷新 ≥ kConfigRefreshRounds 时全量查询；否则命中缓存
     */
    const bool refresh_cfg =
        start_type_cache_.empty() || (round - last_config_refresh_ >= kConfigRefreshRounds);
    if (refresh_cfg) {
        for (ServiceInfo& b : base) {
            b.start_type = query_start_type(b.name);
            start_type_cache_[b.name] = b.start_type;
        }
        last_config_refresh_ = round;
    } else {
        for (ServiceInfo& b : base) {
            const auto it = start_type_cache_.find(b.name);
            if (it != start_type_cache_.end()) {
                b.start_type = it->second;
            }
        }
    }

    /*
     * ========================================================================
     * 步骤3：构建 PID -> 进程样本 映射
     * ========================================================================
     */
    std::unordered_map<std::uint32_t, const ProcessSample*> by_pid;
    by_pid.reserve(processes.size());
    for (const ProcessSample& p : processes) {
        by_pid[p.pid] = &p;
    }

    /*
     * ========================================================================
     * 步骤4：统计同宿主共享数（按 SCM 宿主 PID 聚合，与占用可见性无关）
     * ========================================================================
     * 说明：即使宿主 PID 被拒（D18 "denied"），D11 的「N 个服务共享」标注
     *       仍应显示，故此处统计全部运行中且含宿主 PID 的服务
     */
    std::unordered_map<std::uint32_t, int> share_count;
    for (const ServiceInfo& b : base) {
        if (b.state == "running" && b.host_pid != 0) {
            share_count[b.host_pid] += 1;
        }
    }

    /*
     * ========================================================================
     * 步骤5：产出三态占用
     * ========================================================================
     * 口径（D18）：
     *   - 未运行 / 无宿主        -> "na"
     *   - 宿主 PID 可读          -> "ok"（宿主真实占用 + 共享数）
     *   - 宿主 PID 被拒 / 缺失   -> "denied"
     */
    ServiceInfoList out;
    out.reserve(base.size());
    for (ServiceInfo& b : base) {
        if (b.state != "running" || b.host_pid == 0) {
            b.occ_state = "na";
        } else {
            // 共享数按宿主 PID 聚合，与占用可见性无关（D11 + D18）
            b.shared_count = share_count[b.host_pid];
            const auto it = by_pid.find(b.host_pid);
            if (it != by_pid.end() && it->second->accessible) {
                b.occ_state = "ok";
                b.cpu_percent = it->second->cpu_percent;
                b.working_set_bytes = it->second->working_set_bytes;
            } else {
                b.occ_state = "denied";
            }
        }
        out.push_back(std::move(b));
    }
    return out;
}

}  // namespace perfstat
