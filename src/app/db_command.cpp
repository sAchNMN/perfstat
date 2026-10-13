/*
 * ========================================================================
 * 模块：app / db_command
 * ========================================================================
 * 作用：数据库概况查询与手动保留清理
 */
#include "app/db_command.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "app/paths.h"
#include "store/db.h"

namespace perfstat::app {
namespace {

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
        "用法：perfstat db [选项]\n"
        "  --stats             打印表概况（默认动作）\n"
        "  --purge             立即执行保留清理\n"
        "  --days=<n>          清理保留天数，默认 30（0 表示删除全部已完成分钟）\n"
        "  --db=<path>         数据库路径，默认 %%LOCALAPPDATA%%\\PerfStat\\perfstat.db\n"
        "  --help              显示本帮助\n");
}

// 把 unix 秒格式化成 "YYYY-MM-DD HH:MM:SS"；ts 为 0 时返回 "-"
std::string format_local(std::int64_t ts) {
    if (ts == 0) {
        return "-";
    }
    const std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm_buf{};
    localtime_s(&tm_buf, &t);
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

bool file_exists(const std::string& path) {
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

int run_db_command(const std::vector<std::string>& args) {
    /*
     * ========================================================================
     * 步骤1：解析选项
     * ========================================================================
     */
    bool        do_purge = false;
    bool        do_stats = false;
    int         days = 30;
    std::string target_db;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return EXIT_SUCCESS;
        }
        if (arg == "--purge") {
            do_purge = true;
            continue;
        }
        if (arg == "--stats") {
            do_stats = true;
            continue;
        }
        if (arg.rfind("--days=", 0) == 0) {
            const int value = std::atoi(option_value(arg, "--days", "30").c_str());
            days = value < 0 ? 0 : value;
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
    if (!do_purge && !do_stats) {
        do_stats = true;  // 默认动作
    }

    /*
     * ========================================================================
     * 步骤2：打开数据库
     * ========================================================================
     * 说明：只读查询时若库不存在直接提示，不凭空建库
     */
    if (!file_exists(target_db)) {
        std::printf("数据库不存在：%s\n", target_db.c_str());
        std::printf("先跑一次：perfstat record --duration=70\n");
        return EXIT_FAILURE;
    }

    store::Database db;
    std::string     err;
    if (!db.open(target_db, &err)) {
        std::fprintf(stderr, "[PerfStat] 打开数据库失败：%s\n", err.c_str());
        return EXIT_FAILURE;
    }

    std::printf("数据库：%s\n\n", target_db.c_str());

    /*
     * ========================================================================
     * 步骤3：可选的手动保留清理
     * ========================================================================
     */
    if (do_purge) {
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
        const std::int64_t before = now - static_cast<std::int64_t>(days) * 86400;
        const std::int64_t removed = db.purge_metrics_before(before);
        std::printf("保留清理：删除 %lld 行（保留 %d 天，早于 %s）\n\n",
                    static_cast<long long>(removed), days, format_local(before).c_str());
    }

    /*
     * ========================================================================
     * 步骤4：概况统计
     * ========================================================================
     */
    if (do_stats) {
        store::Database::Stats stats;
        if (!db.stats(&stats, &err)) {
            std::fprintf(stderr, "[PerfStat] 统计失败：%s\n", err.c_str());
            return EXIT_FAILURE;
        }
        std::printf("dim_entity   实体数 : %lld\n", static_cast<long long>(stats.entity_count));
        std::printf("fact_metric_1m 行数 : %lld\n", static_cast<long long>(stats.metric_rows));
        std::printf("最早 bucket         : %s\n", format_local(stats.oldest_bucket).c_str());
        std::printf("最新 bucket         : %s\n", format_local(stats.newest_bucket).c_str());
    }

    return EXIT_SUCCESS;
}

}  // namespace perfstat::app
