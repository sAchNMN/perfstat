/*
 * ========================================================================
 * 步骤1：程序入口与运行模式分发
 * ========================================================================
 * 目标：PerfStat 可执行文件唯一入口
 * 数据源：命令行参数
 * 操作要点：
 *   1) 无参      -> 托盘常驻模式（M8 接入）
 *   2) collect   -> 采集骨架调试，前台打印榜单（M1）
 *   3) record    -> 采集并落库（M2）
 *   4) db        -> 数据库概况与保留清理（M2）
 *   5) 其他子命令 -> 提权 helper，单次执行后退出（M6 接入）
 */

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "app/collect_command.h"
#include "app/db_command.h"
#include "app/record_command.h"

namespace {

// 应用常量
constexpr const char* kAppName = "PerfStat";
constexpr const char* kAppVersion = "0.1.0";

}  // namespace

int main(int argc, char** argv) {
    /*
     * ========================================================================
     * 步骤1：控制台初始化
     * ========================================================================
     * 说明：统一按 UTF-8 输出，避免进程名/路径中文乱码
     */
    SetConsoleOutputCP(CP_UTF8);

    std::printf("[%s] v%s 启动，参数个数=%d\n", kAppName, kAppVersion, argc - 1);

    /*
     * ========================================================================
     * 步骤2：子命令分发
     * ========================================================================
     */
    if (argc > 1) {
        std::vector<std::string> args(argv + 1, argv + argc);
        const std::string& command = args[0];

        // 2.1 采集骨架（M1）
        if (command == "collect") {
            return perfstat::app::run_collect_command(args);
        }

        // 2.2 采集落库（M2）
        if (command == "record") {
            return perfstat::app::run_record_command(args);
        }

        // 2.3 数据库概况与保留清理（M2）
        if (command == "db") {
            return perfstat::app::run_db_command(args);
        }

        // 2.4 提权 helper 占位（M6 接入）
        std::printf("[%s] 模式=提权 helper，子命令=%s（骨架，动作分发待接入 M6）\n", kAppName,
                    command.c_str());
        return EXIT_SUCCESS;
    }

    /*
     * ========================================================================
     * 步骤3：无参模式 = 托盘常驻
     * ========================================================================
     * 说明：M8 接入；当前提示调试子命令
     */
    std::printf("[%s] 模式=托盘常驻（骨架，待接入 M8）\n", kAppName);
    std::printf("[%s] 试运行采集骨架：perfstat collect\n", kAppName);
    std::printf("[%s] 试运行采集落库：perfstat record --duration=70\n", kAppName);
    std::printf("[%s] 查看数据库概况：perfstat db --stats\n", kAppName);
    return EXIT_SUCCESS;
}
