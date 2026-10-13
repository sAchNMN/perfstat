/*
 * ========================================================================
 * 模块：app / collect_command
 * ========================================================================
 * 作用：解析 collect 子命令选项，驱动 Sampler 与控制台输出
 */
#include "app/collect_command.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/console_report.h"
#include "collect/sampler.h"

namespace perfstat::app {
namespace {

// 打开 ANSI 转义支持，用于清屏重绘
void enable_virtual_terminal() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) {
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

// 取 "--key=value" 的 value，缺省返回 fallback
std::string option_value(const std::string& arg, const std::string& key,
                         const std::string& fallback) {
    const std::string prefix = key + "=";
    if (arg.rfind(prefix, 0) == 0) {
        return arg.substr(prefix.size());
    }
    return fallback;
}

void print_usage() {
    std::printf(
        "用法：perfstat collect [选项]\n"
        "  --interval=<ms>  采样间隔毫秒，默认 2000，最小 500\n"
        "  --top=<n>        榜单条数，默认 20\n"
        "  --system         榜单纳入系统关键进程\n"
        "  --bg-only        只显示判定为后台的进程\n"
        "  --help           显示本帮助\n");
}

}  // namespace

int run_collect_command(const std::vector<std::string>& args) {
    /*
     * ========================================================================
     * 步骤1：解析选项
     * ========================================================================
     */
    std::uint32_t interval_ms = 2000;
    ReportOptions report;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return EXIT_SUCCESS;
        }
        if (arg == "--system") {
            report.include_system = true;
            continue;
        }
        if (arg == "--bg-only") {
            report.only_background = true;
            continue;
        }
        if (arg.rfind("--interval=", 0) == 0) {
            const int value = std::atoi(option_value(arg, "--interval", "2000").c_str());
            interval_ms = static_cast<std::uint32_t>(value < 500 ? 500 : value);
            continue;
        }
        if (arg.rfind("--top=", 0) == 0) {
            const int value = std::atoi(option_value(arg, "--top", "20").c_str());
            report.top_n = static_cast<std::size_t>(value < 1 ? 1 : value);
            continue;
        }

        std::fprintf(stderr, "[PerfStat] 未知选项：%s\n", arg.c_str());
        print_usage();
        return EXIT_FAILURE;
    }

    /*
     * ========================================================================
     * 步骤2：初始化控制台与运行组件
     * ========================================================================
     */
    enable_virtual_terminal();

    Sampler sampler(interval_ms);
    ConsoleReporter reporter(report);

    std::printf("PerfStat 采集骨架启动 | 间隔 %ums | Top %zu | 系统关键 %s\n", interval_ms,
                report.top_n, report.include_system ? "显示" : "隐藏");
    std::printf("Ctrl+C 退出\n\n");

    /*
     * ========================================================================
     * 步骤3：跑采样循环
     * ========================================================================
     * 说明：每轮清屏重绘，仅作 M1 人工核对用
     */
    sampler.run([&reporter](const ProcessSampleList& samples, const SampleContext& context) {
        reporter.render(samples, context, true);
    });

    std::printf("\n[PerfStat] 采集已停止\n");
    return EXIT_SUCCESS;
}

}  // namespace perfstat::app
