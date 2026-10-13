/*
 * ========================================================================
 * 模块：collect / process_collector
 * ========================================================================
 * 作用：Toolhelp32 快照枚举进程 + 分项指标采集 + 差分计算
 * 依据：docs/DESIGN.md 6.1 / D6 / D7 / D12
 */
#include "collect/process_collector.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <utility>

namespace perfstat {
namespace {

// FILETIME（100ns 为单位）转无符号整数
std::uint64_t filetime_to_u64(const FILETIME& ft) {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) |
           static_cast<std::uint64_t>(ft.dwLowDateTime);
}

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

// D12：实例键 = name|path|start_time_100ns
std::string make_instance_key(const std::string& name, const std::string& path,
                              std::uint64_t start_100ns) {
    return name + "|" + path + "|" + std::to_string(start_100ns);
}

}  // namespace

ProcessCollector::ProcessCollector() {
    /*
     * ========================================================================
     * 步骤1：初始化运行期常量
     * ========================================================================
     * 目标：逻辑核数（CPU 归一用）、QPC 频率（换算实际间隔）
     */
    cores_ = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (cores_ == 0) {
        cores_ = 1;
    }

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    qpc_freq_ = (freq.QuadPart > 0) ? static_cast<double>(freq.QuadPart) : 1.0;
}

double ProcessCollector::current_wall_seconds() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    double seconds = 0.0;
    if (!first_round_ && last_qpc_ != 0) {
        seconds = static_cast<double>(now.QuadPart - last_qpc_) / qpc_freq_;
    }
    last_qpc_ = now.QuadPart;
    first_round_ = false;
    return seconds;
}

ProcessSampleList ProcessCollector::collect_round() {
    /*
     * ========================================================================
     * 步骤1：取时间基准
     * ========================================================================
     * 说明：首轮返回 0，本轮所有 delta 均为 0，仅建立基准
     */
    const double wall_seconds = current_wall_seconds();

    /*
     * ========================================================================
     * 步骤2：刷新可见顶层窗口集合
     * ========================================================================
     * 用途：D10 后台判定中的「无可见窗口」条件
     */
    filter_.refresh_visible_windows();

    /*
     * ========================================================================
     * 步骤3：枚举进程并逐项读取指标
     * ========================================================================
     * 数据源：CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)
     * 操作：
     *   1) 遍历进程快照
     *   2) 逐个填充样本与 delta
     */
    ProcessSampleList out;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return out;
    }

    PrevMap next;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    // 3.1 遍历快照
    if (Process32FirstW(snapshot, &entry)) {
        do {
            ProcessSample sample;
            sample.pid = entry.th32ProcessID;
            sample.ppid = entry.th32ParentProcessID;
            sample.name = wide_to_utf8(entry.szExeFile);
            read_process(sample, wall_seconds, next);
            out.push_back(std::move(sample));
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);

    /*
     * ========================================================================
     * 步骤4：滚动基准表
     * ========================================================================
     * 说明：只保留本轮仍存活的进程；已退出进程的基准自然丢弃
     */
    prev_.swap(next);
    return out;
}

void ProcessCollector::read_process(ProcessSample& sample, double wall_seconds, PrevMap& next) {
    /*
     * ========================================================================
     * 步骤1：打开进程句柄
     * ========================================================================
     * 策略：优先申请 VM_READ 以读取内存；失败退化为仅查询信息
     * 失败时仍返回分类标记，指标留 0
     */
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                                sample.pid);
    if (handle == nullptr) {
        handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, sample.pid);
    }
    if (handle == nullptr) {
        classify(sample);
        return;
    }
    sample.accessible = true;

    /*
     * ========================================================================
     * 步骤2：读取完整路径
     * ========================================================================
     * 接口：QueryFullProcessImageNameW
     */
    std::wstring wide_path(32768, L'\0');
    DWORD path_len = static_cast<DWORD>(wide_path.size());
    if (QueryFullProcessImageNameW(handle, 0, wide_path.data(), &path_len)) {
        wide_path.resize(path_len);
        sample.path = wide_to_utf8(wide_path.c_str());
    }

    /*
     * ========================================================================
     * 步骤3：读取时间、IO、内存三组指标
     * ========================================================================
     * 接口：
     *   1) GetProcessTimes      -> 内核态/用户态累计时间、创建时间
     *   2) GetProcessIoCounters -> 累计读写字节
     *   3) GetProcessMemoryInfo -> 工作集、私有提交
     */
    FILETIME create_ft{};
    FILETIME exit_ft{};
    FILETIME kernel_ft{};
    FILETIME user_ft{};
    IO_COUNTERS io{};
    PROCESS_MEMORY_COUNTERS mem{};

    const bool has_times = GetProcessTimes(handle, &create_ft, &exit_ft, &kernel_ft, &user_ft) != 0;
    const bool has_io = GetProcessIoCounters(handle, &io) != 0;
    if (GetProcessMemoryInfo(handle, &mem, sizeof(mem))) {
        sample.working_set_bytes = mem.WorkingSetSize;
        sample.private_bytes = mem.PagefileUsage;
    }

    CloseHandle(handle);

    /*
     * ========================================================================
     * 步骤4：算差分（CPU 与 IO）
     * ========================================================================
     * 前提：上一轮存在同 PID 记录，且实例键一致（防 PID 复用串号）
     * CPU 公式：cpu% = (Δkernel + Δuser) / (Δwall × 逻辑核数) × 100
     */
    if (has_times) {
        sample.start_time_100ns = filetime_to_u64(create_ft);
        sample.instance_key = make_instance_key(sample.name, sample.path, sample.start_time_100ns);

        const std::uint64_t kernel = filetime_to_u64(kernel_ft);
        const std::uint64_t user = filetime_to_u64(user_ft);

        const auto base = prev_.find(sample.pid);
        const bool has_base = base != prev_.end() && !sample.instance_key.empty() &&
                              base->second.instance_key == sample.instance_key;

        // 4.1 CPU 差分
        if (has_base && wall_seconds > 0.0) {
            const std::uint64_t delta_kernel = kernel - base->second.kernel_100ns;
            const std::uint64_t delta_user = user - base->second.user_100ns;
            const double cpu_seconds = static_cast<double>(delta_kernel + delta_user) * 1e-7;
            sample.cpu_percent = cpu_seconds / (wall_seconds * cores_) * 100.0;
        }

        // 4.2 记录本轮基准
        PrevState state;
        state.kernel_100ns = kernel;
        state.user_100ns = user;
        state.instance_key = sample.instance_key;
        if (has_io) {
            state.io_read = io.ReadTransferCount;
            state.io_write = io.WriteTransferCount;
        }
        next[sample.pid] = std::move(state);
    }

    // 4.3 IO 差分
    if (has_io) {
        sample.io_read_bytes = io.ReadTransferCount;
        sample.io_write_bytes = io.WriteTransferCount;

        const auto base = prev_.find(sample.pid);
        if (base != prev_.end() && !sample.instance_key.empty() &&
            base->second.instance_key == sample.instance_key) {
            sample.io_read_delta = io.ReadTransferCount - base->second.io_read;
            sample.io_write_delta = io.WriteTransferCount - base->second.io_write;
        }
    }

    /*
     * ========================================================================
     * 步骤5：打分类标记
     * ========================================================================
     */
    classify(sample);
}

void ProcessCollector::classify(ProcessSample& sample) {
    sample.has_visible_window = filter_.has_visible_window(sample.pid);
    sample.is_service_host = filter_.is_service_host(sample.pid);
    sample.is_system_critical = filter_.is_system_critical(sample.name);
    sample.is_background = filter_.is_background(sample.pid, sample.name);
}

}  // namespace perfstat
