/*
 * ========================================================================
 * 模块：collect / sampler
 * ========================================================================
 * 作用：Waitable Timer 驱动的采样主循环
 * 依据：docs/DESIGN.md 6.1 / 10
 */
#include "collect/sampler.h"

#include <windows.h>

#include <cstdio>

namespace perfstat {
namespace {

// Ctrl+C 处理器需要的停止标志，指向 Sampler 实例内部标志
std::atomic<bool>* g_stop_flag = nullptr;

BOOL WINAPI console_ctrl_handler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            if (g_stop_flag != nullptr) {
                g_stop_flag->store(true);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

}  // namespace

Sampler::Sampler(std::uint32_t interval_ms) : interval_ms_(interval_ms) {}

void Sampler::request_stop() { stop_.store(true); }

void Sampler::run(const Handler& handler) {
    /*
     * ========================================================================
     * 步骤1：注册控制台中断处理
     * ========================================================================
     * 目的：Ctrl+C 时置停止标志，让循环优雅收尾
     */
    g_stop_flag = &stop_;
    SetConsoleCtrlHandler(&console_ctrl_handler, TRUE);

    /*
     * ========================================================================
     * 步骤2：创建高精度可等待定时器
     * ========================================================================
     * 说明：优先高精度标志；老系统不支持时退化为普通定时器
     */
    DWORD flags = 0;
#ifdef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    flags = CREATE_WAITABLE_TIMER_HIGH_RESOLUTION;
#endif

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, flags, TIMER_ALL_ACCESS);
    if (timer == nullptr) {
        timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
    if (timer == nullptr) {
        std::fprintf(stderr, "[PerfStat] 定时器创建失败，err=%lu\n", GetLastError());
        SetConsoleCtrlHandler(&console_ctrl_handler, FALSE);
        g_stop_flag = nullptr;
        return;
    }

    LARGE_INTEGER due{};
    due.QuadPart = 0;  // 相对时间 0 -> 立即触发首轮
    if (!SetWaitableTimer(timer, &due, static_cast<LONG>(interval_ms_), nullptr, nullptr, FALSE)) {
        std::fprintf(stderr, "[PerfStat] 定时器设置失败，err=%lu\n", GetLastError());
        CloseHandle(timer);
        SetConsoleCtrlHandler(&console_ctrl_handler, FALSE);
        g_stop_flag = nullptr;
        return;
    }

    /*
     * ========================================================================
     * 步骤3：采样主循环
     * ========================================================================
     * 操作：
     *   1) 分片等待，保证能及时响应停止标志
     *   2) 定时到点则采集一轮并回调
     */
    SampleContext context;
    context.logical_cores = collector_.logical_cores();
    context.interval_seconds = static_cast<double>(interval_ms_) / 1000.0;

    while (!stop_.load()) {
        const DWORD wait_result = WaitForSingleObject(timer, 250);
        if (wait_result == WAIT_OBJECT_0) {
            ProcessSampleList samples = collector_.collect_round();
            context.round += 1;
            handler(samples, context);
        } else if (wait_result != WAIT_TIMEOUT) {
            break;  // 定时器句柄异常
        }
    }

    /*
     * ========================================================================
     * 步骤4：清理
     * ========================================================================
     */
    CancelWaitableTimer(timer);
    CloseHandle(timer);
    SetConsoleCtrlHandler(&console_ctrl_handler, FALSE);
    g_stop_flag = nullptr;
}

}  // namespace perfstat
