/*
 * ========================================================================
 * 模块：app / console_report
 * ========================================================================
 * 作用：M1 控制台榜单渲染
 */
#include "app/console_report.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <vector>

namespace perfstat {
namespace {

// 字节转 MB（保留计算精度）
double bytes_to_mb(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

// 字节增量转 KB/s
double delta_to_kbps(std::uint64_t delta_bytes, double interval_seconds) {
    if (interval_seconds <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(delta_bytes) / 1024.0 / interval_seconds;
}

// 当前本地时间字符串
std::string now_string() {
    std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &t);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

}  // namespace

ConsoleReporter::ConsoleReporter(ReportOptions options) : options_(options) {}

void ConsoleReporter::render(const ProcessSampleList& samples, const SampleContext& context,
                             bool clear_screen) {
    /*
     * ========================================================================
     * 步骤1：汇总整机口径
     * ========================================================================
     * 目标：CPU 合计、内存合计、后台/系统关键进程计数
     */
    double   total_cpu = 0.0;
    std::uint64_t total_mem = 0;
    std::size_t background_count = 0;
    std::size_t system_count = 0;
    std::size_t inaccessible = 0;

    for (const auto& sample : samples) {
        total_cpu += sample.cpu_percent;
        total_mem += sample.working_set_bytes;
        if (sample.is_background) {
            ++background_count;
        }
        if (sample.is_system_critical) {
            ++system_count;
        }
        if (!sample.accessible) {
            ++inaccessible;
        }
    }

    /*
     * ========================================================================
     * 步骤2：筛选与排序
     * ========================================================================
     * 规则：排除不可读样本；按配置过滤系统关键/非后台；按 CPU 降序取前 N
     */
    std::vector<const ProcessSample*> rows;
    rows.reserve(samples.size());
    for (const auto& sample : samples) {
        if (!sample.accessible) {
            continue;
        }
        if (sample.is_system_critical && !options_.include_system) {
            continue;
        }
        if (options_.only_background && !sample.is_background) {
            continue;
        }
        rows.push_back(&sample);
    }

    std::sort(rows.begin(), rows.end(), [](const ProcessSample* a, const ProcessSample* b) {
        return a->cpu_percent > b->cpu_percent;
    });
    if (rows.size() > options_.top_n) {
        rows.resize(options_.top_n);
    }

    /*
     * ========================================================================
     * 步骤3：输出
     * ========================================================================
     */
    if (clear_screen) {
        std::fputs("\x1b[2J\x1b[H", stdout);
    }

    const std::string stamp = now_string();
    std::printf("PerfStat 采集骨架 | 第 %llu 轮 | 间隔 %.1fs | 逻辑核 %u | %s\n",
                static_cast<unsigned long long>(context.round), context.interval_seconds,
                context.logical_cores, stamp.c_str());
    std::printf("进程 %zu | 后台 %zu | 系统关键 %zu | 不可读 %zu | 整机CPU 合计 %.2f%% | 内存合计 %.1f MB\n\n",
                samples.size(), background_count, system_count, inaccessible, total_cpu,
                bytes_to_mb(total_mem));

    if (context.round <= 1) {
        std::printf("（首轮建立基准，CPU 与 IO 增量为 0，下一轮起有效）\n\n");
    }

    std::printf("%7s %8s %11s %10s %10s %4s  %s\n", "PID", "CPU%", "内存MB", "读KB/s", "写KB/s",
                "后台", "名称");

    for (const auto* row : rows) {
        std::printf("%7u %8.2f %11.1f %10.1f %10.1f %4s  %s\n", row->pid, row->cpu_percent,
                    bytes_to_mb(row->working_set_bytes),
                    delta_to_kbps(row->io_read_delta, context.interval_seconds),
                    delta_to_kbps(row->io_write_delta, context.interval_seconds),
                    row->is_background ? "Y" : "N", row->name.c_str());
    }

    std::printf("\nCtrl+C 退出\n");
    std::fflush(stdout);
}

}  // namespace perfstat
