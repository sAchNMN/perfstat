/*
 * ========================================================================
 * 模块：app / serve_command
 * ========================================================================
 * 作用：常驻后端 —— 采样循环驱动快照广播与落库，HTTP 服务在后台线程
 * 链路：
 *   Sampler 每轮 ──┬── SnapshotStore 更新 ──> /api/processes（HTTP 线程读）
 *                  ├── SseHub 广播        ──> /api/realtime
 *                  └── MinuteAggregator   ──> PersistWorker 落库
 * 依据：docs/DESIGN.md 5 / 13（M3）
 */
#include "app/serve_command.h"

#include <windows.h>
#include <shellapi.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <string>

#include "api/serialize.h"
#include "api/server.h"
#include "api/service_store.h"
#include "api/snapshot_store.h"
#include "api/sse_hub.h"
#include "app/paths.h"
#include "collect/sampler.h"
#include "collect/service_collector.h"
#include "store/aggregator.h"
#include "store/persist_worker.h"

namespace perfstat::app {
namespace {

constexpr int kDefaultPort = 17321;

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
        "用法：perfstat serve [选项]\n"
        "  --port=<n>          监听端口，默认 17321，占用则自动 +1\n"
        "  --interval=<ms>     采样间隔毫秒，默认 2000，最小 500\n"
        "  --db=<path>         数据库路径，默认 %%LOCALAPPDATA%%\\PerfStat\\perfstat.db\n"
        "  --retain-days=<n>   事实行保留天数，默认 30\n"
        "  --no-persist        只服务实时视图，不落库\n"
        "  --no-token          关闭 token 鉴权（仅本机调试）\n"
        "  --open              启动后用默认浏览器打开带 token 的地址\n"
        "  --help              显示本帮助\n");
}

// 生成 32 字节随机 token（64 个十六进制字符）
std::string make_token() {
    std::random_device device;
    std::mt19937_64    generator(device());
    std::uniform_int_distribution<int> dist(0, 15);
    static const char kHex[] = "0123456789abcdef";

    std::string token;
    token.reserve(64);
    for (int i = 0; i < 64; ++i) {
        token.push_back(kHex[dist(generator)]);
    }
    return token;
}

}  // namespace

int run_serve_command(const std::vector<std::string>& args) {
    /*
     * ========================================================================
     * 步骤1：解析选项
     * ========================================================================
     */
    int         port = kDefaultPort;
    std::uint32_t interval_ms = 2000;
    int         retain_days = 30;
    bool        persist = true;
    bool        use_token = true;
    bool        open_browser = false;
    std::string target_db;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return EXIT_SUCCESS;
        }
        if (arg == "--no-persist") {
            persist = false;
            continue;
        }
        if (arg == "--no-token") {
            use_token = false;
            continue;
        }
        if (arg == "--open") {
            open_browser = true;
            continue;
        }
        if (arg.rfind("--port=", 0) == 0) {
            const int value = std::atoi(option_value(arg, "--port", "17321").c_str());
            port = (value < 1 || value > 65535) ? kDefaultPort : value;
            continue;
        }
        if (arg.rfind("--interval=", 0) == 0) {
            const int value = std::atoi(option_value(arg, "--interval", "2000").c_str());
            interval_ms = static_cast<std::uint32_t>(value < 500 ? 500 : value);
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

    std::string err;
    if (!ensure_data_dirs(&err)) {
        std::fprintf(stderr, "[PerfStat] %s\n", err.c_str());
        return EXIT_FAILURE;
    }

    /*
     * ========================================================================
     * 步骤2：准备 token 与服务组件
     * ========================================================================
     */
    const std::string token = use_token ? make_token() : std::string();

    api::SnapshotStore snapshot_store;
    api::ServiceStore   service_store;
    api::SseHub        sse_hub;

    api::ServerOptions server_options;
    server_options.port = port;
    server_options.token = token;
    api::HttpServer http(std::move(server_options), &snapshot_store, &service_store, &sse_hub);

    if (!http.bind(&err)) {
        std::fprintf(stderr, "[PerfStat] %s\n", err.c_str());
        return EXIT_FAILURE;
    }
    http.start();

    /*
     * ========================================================================
     * 步骤3：可选落库线程
     * ========================================================================
     */
    store::PersistWorker worker;
    if (persist) {
        if (!worker.start(target_db, retain_days, &err)) {
            std::fprintf(stderr, "[PerfStat] 写库线程启动失败：%s\n", err.c_str());
            http.stop();
            return EXIT_FAILURE;
        }
    }

    /*
     * ========================================================================
     * 步骤4：打印访问地址
     * ========================================================================
     */
    const std::string url = "http://127.0.0.1:" + std::to_string(http.bound_port()) + "/" +
                            (token.empty() ? std::string() : "?t=" + token);
    std::printf("PerfStat 后端已启动\n");
    std::printf("  访问地址：%s\n", url.c_str());
    std::printf("  采样间隔：%ums | 落库：%s | 鉴权：%s\n", interval_ms,
                persist ? target_db.c_str() : "关闭", token.empty() ? "关闭" : "开启");
    std::printf("Ctrl+C 退出\n\n");
    std::fflush(stdout);  // 输出被重定向到文件时是块缓冲，不刷就拿不到 token

    if (open_browser) {
        // 用系统默认浏览器打开带 token 的地址
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    /*
     * ========================================================================
     * 步骤5：采样主循环
     * ========================================================================
     * 操作：
     *   1) 每轮刷新快照
     *   2) 有 SSE 订阅者才序列化并广播（无订阅者时省掉 JSON 开销）
     *   3) 跨分钟则把完整桶交给写库线程
     */
    Sampler sampler(interval_ms);
    store::MinuteAggregator aggregator;
    ServiceCollector service_collector;

    sampler.run([&](const perfstat::ProcessSampleList& samples,
                    const perfstat::SampleContext& context) {
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

        api::Snapshot snapshot;
        snapshot.ts = now;
        snapshot.round = context.round;
        snapshot.logical_cores = context.logical_cores;
        snapshot.processes = samples;

        // 5.1 有订阅者才广播
        if (sse_hub.subscriber_count() > 0) {
            sse_hub.broadcast(api::serialize_processes(snapshot));
        }

        // 5.2 刷新进程快照
        snapshot_store.update(std::move(snapshot));

        // 5.3 刷新服务快照（D18 三态占用，关联本轮进程快照）
        api::ServiceSnapshot service_snapshot;
        service_snapshot.ts = now;
        service_snapshot.round = context.round;
        service_snapshot.services = service_collector.collect_round(samples, context.round);
        service_store.update(std::move(service_snapshot));

        // 5.4 落库
        if (persist) {
            store::MinuteBucket bucket;
            if (aggregator.add_round(samples, now, &bucket)) {
                worker.submit(std::move(bucket));
            }
        }
    });

    /*
     * ========================================================================
     * 步骤6：收尾
     * ========================================================================
     */
    if (persist) {
        store::MinuteBucket tail = aggregator.flush();
        if (!tail.rows.empty()) {
            worker.submit(std::move(tail));
        }
        worker.stop();
    }
    http.stop();

    std::printf("\n[PerfStat] 后端已停止\n");
    if (persist) {
        std::printf("[PerfStat] 写入事实行 %lld 行 | 丢弃桶 %lld | 清理 %lld 行\n",
                    static_cast<long long>(worker.written_rows()),
                    static_cast<long long>(worker.dropped_buckets()),
                    static_cast<long long>(worker.purged_rows()));
    }
    return EXIT_SUCCESS;
}

}  // namespace perfstat::app
