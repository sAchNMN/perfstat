/*
 * ========================================================================
 * 模块：collect / process_metrics
 * ========================================================================
 * 作用：定义采集层的数据模型，供采集器、调度器、输出层共用
 * 依据：
 *   docs/DESIGN.md 6.1（进程采集）
 *   docs/DESIGN.md 6.3（后台判定）
 *   D7（统计口径）/ D12（进程标识）
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace perfstat {

/*
 * ========================================================================
 * 类型：ProcessSample
 * ========================================================================
 * 含义：单个进程在某一轮采样中的快照
 * 数据源：
 *   1) GetProcessTimes        -> cpu / start_time
 *   2) GetProcessMemoryInfo   -> working_set / private
 *   3) GetProcessIoCounters   -> io 累计与增量
 *   4) QueryFullProcessImageName -> path
 *   父进程 PID 来自枚举用的 PROCESSENTRY32W.th32ParentProcessID
 * 说明：
 *   - cpu_percent 为「占整机百分比」，分母按逻辑核数归一
 *   - 首轮无上一轮基准，cpu_percent 与 io_*_delta 均为 0
 */
struct ProcessSample {
    std::uint32_t pid{0};
    std::uint32_t ppid{0};       // 父进程 PID（构建进程树用），0 表示无父/已退出
    std::string   name;          // 进程名，含扩展名
    std::string   path;          // 可执行文件完整路径，取不到则为空
    std::string   instance_key;  // D12：name|path|start_time_100ns

    double        cpu_percent{0.0};      // 占整机百分比
    std::uint64_t working_set_bytes{0};  // 工作集
    std::uint64_t private_bytes{0};      // 私有提交
    std::uint64_t io_read_bytes{0};      // 累计读字节
    std::uint64_t io_write_bytes{0};     // 累计写字节
    std::uint64_t io_read_delta{0};      // 本轮读增量
    std::uint64_t io_write_delta{0};     // 本轮写增量

    std::uint64_t start_time_100ns{0};   // 进程创建时间（FILETIME 转 100ns）

    // 标记位
    bool accessible{false};          // 是否成功打开句柄并读到指标
    bool has_visible_window{false};  // 是否拥有可见顶层窗口
    bool is_service_host{false};     // 是否为服务宿主（M4 接入）
    bool is_system_critical{false};  // 是否命中系统关键进程黑名单
    bool is_background{false};       // D10 判定结果
};

using ProcessSampleList = std::vector<ProcessSample>;

/*
 * ========================================================================
 * 类型：SampleContext
 * ========================================================================
 * 含义：一轮采样的上下文，随样本一起传给输出层
 */
struct SampleContext {
    std::uint64_t round{0};                 // 第几轮，从 1 起
    std::uint32_t logical_cores{1};         // 逻辑核数
    double        interval_seconds{2.0};    // 本轮与上一轮的实际间隔（秒）
};

}  // namespace perfstat
