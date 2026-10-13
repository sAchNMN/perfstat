/*
 * ========================================================================
 * 模块：app / record_command
 * ========================================================================
 * 作用：采集主循环 + 分钟聚合 + 异步落库
 * 链路：
 *   Sampler 每轮 -> MinuteAggregator 累加 -> 跨分钟产出桶
 *                -> PersistWorker 队列 -> 写库线程落库
 * 依据：docs/DESIGN.md 6.4 / 13（M2）
 */
#include "app/record_command.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "app/paths.h"
#include "collect/sampler.h"
#include "store/aggregator.h"
#include "store/persist_worker.h"

namespace perfstat::app {
namespace {

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
        "用法：perfstat record [选项]\n"
        "  --interval=<ms>     采样间隔毫秒，默认 2000，最小 500\n"
        "  --duration=<sec>    运行时长秒数，默认 0 表示一直跑到 Ctrl+C\n"
        "  --db=<path>         数据库路径，默认 %%LOCALAPPDATA%%\\PerfStat\\perfstat.db\n"
        "  --retain-days=<n>   事实行保留天数，默认 30\n"
        "  --help              显示本帮助\n");
}

// 把 unix 秒格式化成 "YYYY-MM-DD HH:MM:SS"
std::string format_local(std::int64_t ts) {
    const std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm_buf{};
    localtime_s(&tm_buf, &t);
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

}  // namespace

int run_record_command(const std::vector<std::string>& args) {
    /*
     * ========================================================================
     * 步骤1：解析选项
     * ========================================================================
     */
    std::uint32_t interval_ms = 2000;
    int           duration_sec = 0;
    int           retain_days = 30;
    std::string   target_db;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return EXIT_SUCCESS;
        }
        if (arg.rfind("--interval=", 0) == 0) {
            const int value = std::atoi(option_value(arg, "--interval", "2000").c_str());
            interval_ms = static_cast<std::uint32_t>(value < 500 ? 500 : value);
            continue;
        }
        if (arg.rfind("--duration=", 0) == 0) {
            const int value = std::atoi(option_value(arg, "--duration", "0").c_str());
            duration_sec = value < 0 ? 0 : value;
            continue;
        }
        if (arg.rfind("--retain-days=", 0) == 0) {
            const int value = std::atoi(option_value(arg, "--retain-days", "30").c_str());
            retain_days = value < 0 ? 0 : value;
            continue;
        }
        if (arg.rfind("--db=", 0) == 0) {
            target_db = option_value(arg, "--db", "");
            continue;
        }

        std::fprintf(stderr, "[PerfStat] 未知选项：%s\n", arg.c_str());
        print_usage();
        return EXIT_FAILURE;
    }

    if (target_db.empty()) {
        target_db = db_path();
    }

    /*
     * ========================================================================
     * 步骤2：准备数据目录与写库线程
     * ========================================================================
     */
    std::string err;
    if (!ensure_data_dirs(&err)) {
        std::fprintf(stderr, "[PerfStat] %s\n", err.c_str());
        return EXIT_FAILURE;
    }

    store::PersistWorker worker;
    if (!worker.start(target_db, retain_days, &err)) {
        std::fprintf(stderr, "[PerfStat] 写库线程启动失败：%s\n", err.c_str());
        return EXIT_FAILURE;
    }

    /*
     * ========================================================================
     * 步骤3：跑采集主循环
     * ========================================================================
     * 操作：
     *   1) 每轮样本交给聚合器，跨分钟时取得完整桶并投递写库
     *   2) 到达 duration 上限则请求停止
     */
    Sampler sampler(interval_ms);
    store::MinuteAggregator aggregator;

    const std::int64_t start_ts = static_cast<std::int64_t>(std::time(nullptr));
    std::printf("PerfStat 采集落库启动 | 间隔 %ums | 保留 %d 天\n", interval_ms, retain_days);
    std::printf("数据库：%s\n", target_db.c_str());
    std::printf("起算时间 %s，跨分钟后写库；Ctrl+C 退出\n\n", format_local(start_ts).c_str());

    sampler.run([&](const perfstat::ProcessSampleList& samples,
                    const perfstat::SampleContext& context) {
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

        // 3.1 并入本轮，必要时取回上一分钟桶
        store::MinuteBucket bucket;
        if (aggregator.add_round(samples, now, &bucket)) {
            const std::int64_t bucket_ts = bucket.bucket_ts;
            const std::size_t  row_count = bucket.rows.size();
            worker.submit(std::move(bucket));
            std::printf("第 %llu 轮 | 完成分钟 %s | 落库 %zu 个实体\n",
                        static_cast<unsigned long long>(context.round),
                        format_local(bucket_ts).c_str(), row_count);
        } else if (context.round % 15 == 0) {
            std::printf("第 %llu 轮 | 进行中 %s | 本轮进程 %zu\n",
                        static_cast<unsigned long long>(context.round),
                        format_local(now).c_str(), samples.size());
        }

        // 3.2 时长上限
        if (duration_sec > 0 && (now - start_ts) >= duration_sec) {
            sampler.request_stop();
        }
    });

    /*
     * ========================================================================
     * 步骤4：收尾
     * ========================================================================
     * 操作：
     *   1) 把不足一分钟的残留桶也落库
     *   2) 停写库线程（排空队列）
     */
    store::MinuteBucket tail = aggregator.flush();
    if (!tail.rows.empty()) {
        worker.submit(std::move(tail));
    }
    worker.stop();

    std::printf("\n[PerfStat] 采集已停止\n");
    std::printf("[PerfStat] 写入事实行 %lld 行 | 丢弃桶 %lld | 清理 %lld 行\n",
                static_cast<long long>(worker.written_rows()),
                static_cast<long long>(worker.dropped_buckets()),
                static_cast<long long>(worker.purged_rows()));
    return EXIT_SUCCESS;
}

}  // namespace perfstat::app
