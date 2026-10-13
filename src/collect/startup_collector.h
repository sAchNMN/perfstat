/*
 * ========================================================================
 * 模块：collect / startup_collector
 * ========================================================================
 * 作用：枚举开机自启项（注册表 Run + 启动文件夹），并解析启用状态与目标程序
 * 依据：
 *   docs/DESIGN.md 6.6（启动项采集）
 *   docs/DESIGN.md 12（/startup 页面）
 * 说明：
 *   - 全程只读、零提权：HKCU 与 HKLM 的 Run 普通用户都可读
 *   - 五种来源：HKCU Run / HKLM Run(64) / HKLM Run(32) / 用户启动文件夹 / 公共启动文件夹
 *   - 启用状态取自 Explorer\StartupApproved，与任务管理器「启动应用」页判定一致
 *   - 启动文件夹里的 .lnk 用 IShellLink 解析目标，解析不到就留空
 *   - 从命令串抽出 exe 路径（支持带引号 / 不带引号 / %ENV% 展开），
 *     供序列化时与进程快照关联，得到「该启动项当前是否在跑、占多少」
 *   - 本结构只描述静态清单，不含占用；占用在序列化阶段现算，避免缓存过期
 */
#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include <windows.h>

namespace perfstat {

/*
 * ========================================================================
 * 类型：StartupInfo
 * ========================================================================
 * 字段：
 *   - name     ：注册表值名 / 启动文件夹里的文件名（去掉 .lnk）
 *   - command  ：原始命令（注册表值数据 / 快捷方式的目标 + 参数）
 *   - exe_path ：从 command 抽出的可执行文件路径，空表示没解析出来
 *   - source   ：人类可读的来源标签，如 "HKLM Run（64 位）"
 *   - scope    ：user（当前用户）| machine（所有用户）
 *   - kind     ：registry | folder
 *   - enabled  ：是否启用（false = 被 Explorer 的「启动应用」关掉了）
 */
struct StartupInfo {
    std::string name;
    std::string command;
    std::string exe_path;
    std::string source;
    std::string scope;
    std::string kind;
    bool        enabled{true};
};

using StartupInfoList = std::vector<StartupInfo>;

class StartupCollector {
public:
    /*
     * ====================================================================
     * 步骤：采集一次
     * ====================================================================
     * 返回：全部自启项（静态清单，不含占用）
     * 线程安全：无长期共享状态，可在任意线程调用
     * 备注：含 COM 调用（解析 .lnk），内部自行 CoInitializeEx / CoUninitialize
     */
    StartupInfoList collect();

private:
    // 读一处注册表 Run 键（view 取 KEY_WOW64_64KEY / KEY_WOW64_32KEY）
    // tag 标识该处对应的 StartupApproved 作用域，见 .cpp 的 kTag*
    void read_run_key(HKEY hive, REGSAM view, const std::string& tag,
                      const std::string& source_label, const std::string& scope,
                      StartupInfoList& out);

    // 读一处 StartupApproved 键，把被禁用的值名收进 disabled_（键为 tag + 名）
    void read_approved_key(HKEY hive, REGSAM view, bool is_folder, const std::string& tag);

    // 遍历一个启动文件夹（env_name 为 APPDATA / ProgramData）
    void read_startup_folder(const std::string& env_name, const std::string& tag,
                             const std::string& source_label, const std::string& scope,
                             StartupInfoList& out);

    /*
     * 被禁用的条目键，形如 "hl-run64\x1feverything"（名已转小写）
     * 必须按作用域分桶：StartupApproved 里会残留「Run 值早已删除」的历史记录，
     * 若把各 hive / 视图揉成一个集合按名查找，HKCU 的残留会误伤 HKLM 的同名条目
     */
    std::unordered_set<std::string> disabled_;
};

}  // namespace perfstat
