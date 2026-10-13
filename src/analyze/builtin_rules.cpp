/*
 * ========================================================================
 * 模块：analyze / builtin_rules
 * ========================================================================
 * 作用：内置规则表数据与匹配实现
 * 依据：docs/DESIGN.md 6.5 / D13
 * 说明：
 *   - 全部 match 键写小写 ASCII；匹配前对实体名做同等小写化
 *   - 规则粒度只到「软件/组件」这一层，不细分到具体版本，避免维护负担
 */
#include "analyze/builtin_rules.h"

#include <cstring>

#include "analyze/usage_types.h"

namespace perfstat::analyze {
namespace {

/*
 * ========================================================================
 * 数据：进程内置规则
 * ========================================================================
 * 覆盖四类：
 *   1) Windows 自身常驻（外壳、系统核心、更新）
 *   2) 安全软件（Defender 与第三方）
 *   3) 常见第三方常驻（同步盘、通讯、输入法、开发工具）
 *   4) 驱动与固件组件
 */
const BuiltinRule kProcessRules[] = {
    // ---- Windows 自身常驻 ----
    {"svchost.exe", "系统服务", "多个 Windows 系统服务的共享宿主进程", "不建议结束，结束会连带停止其承载的服务"},
    {"dwm.exe", "桌面外壳", "桌面窗口管理器，负责窗口合成与渲染", "不建议结束，结束会导致黑屏并强制注销"},
    {"explorer.exe", "桌面外壳", "资源管理器，负责桌面、任务栏与文件浏览", "不建议结束，结束会丢失任务栏与桌面图标"},
    {"sihost.exe", "桌面外壳", "Shell 基础结构主机", "系统组件，无需处理"},
    {"shellexperiencehost.exe", "桌面外壳", "开始菜单与系统弹窗的托管宿主", "系统组件，无需处理"},
    {"startmenuexperiencehost.exe", "桌面外壳", "开始菜单进程", "系统组件，无需处理"},
    {"searchhost.exe", "桌面外壳", "搜索界面宿主", "系统组件，无需处理"},
    {"widgets.exe", "桌面外壳", "小组件面板", "不用小组件可在任务栏设置里关闭"},
    {"widgetservice.exe", "桌面外壳", "小组件服务宿主", "不用小组件可关闭"},
    {"yourphone.exe", "桌面外壳", "手机连接（Your Phone）", "不用可卸载「手机连接」应用"},
    {"phoneexperiencehost.exe", "桌面外壳", "手机连接宿主进程", "不用可卸载"},
    {"runtimebroker.exe", "系统核心", "为商店应用代行受限权限操作", "系统组件，出现多个实例属正常"},
    {"taskhostw.exe", "系统核心", "任务宿主，承载后台计划任务", "系统组件，无需处理"},
    {"conhost.exe", "系统核心", "控制台窗口宿主", "随命令行程序出现，无需处理"},
    {"wmiprvse.exe", "系统核心", "WMI 提供程序宿主", "系统组件；持续高占用说明有软件在频繁查询 WMI"},
    {"audiodg.exe", "系统核心", "音频设备图形隔离进程", "音频播放时出现，不建议结束"},
    {"cmd.exe", "系统核心", "Windows 命令提示符", "确认来源后再结束"},
    {"powershell.exe", "系统核心", "Windows PowerShell", "确认来源后再结束"},
    {"pwsh.exe", "系统核心", "PowerShell 7", "确认来源后再结束"},
    {"spoolsv.exe", "系统服务", "打印后台处理程序", "不用打印机可停用 Print Spooler 服务"},
    {"officeclicktorun.exe", "更新与维护", "微软 Office 即点即用更新与维护", "不用 Office 可卸载后消失"},
    {"microsoftedgeupdate.exe", "更新与维护", "Edge 自动更新进程", "占用极低，一般无需处理"},
    {"googleupdate.exe", "更新与维护", "Google 系更新程序", "占用极低，一般无需处理"},
    {"windowsupdate.exe", "更新与维护", "Windows 更新界面进程", "仅在更新时出现"},

    // ---- 输入与辅助 ----
    {"ctfmon.exe", "输入与辅助", "文本服务框架，支撑输入法与语言栏", "系统组件，无需处理"},
    {"textinputhost.exe", "输入与辅助", "输入法候选窗与触摸键盘宿主", "系统组件，无需处理"},
    {"wetype.exe", "输入与辅助", "微信输入法", "不用可卸载"},
    {"sogoucloud.exe", "输入与辅助", "搜狗输入法云服务组件", "不用云输入可在输入法设置里关闭"},
    {"sgtool.exe", "输入与辅助", "搜狗输入法工具进程", "不用可在输入法设置里关闭"},

    // ---- 安全防护 ----
    {"msmpeng.exe", "安全防护", "Microsoft Defender 反恶意软件引擎", "不建议结束，结束会中断实时防护"},
    {"nissrv.exe", "安全防护", "Microsoft Defender 网络检查服务", "不建议结束"},
    {"securityhealthservice.exe", "安全防护", "Windows 安全中心服务", "系统组件，无需处理"},
    {"securityhealthsystray.exe", "安全防护", "安全中心托盘图标", "系统组件，无需处理"},
    {"360tray.exe", "安全防护", "360 安全卫士主进程", "不用可卸载；常驻内存与 CPU 开销偏大"},
    {"zhudongfangyu.exe", "安全防护", "360 主动防御服务", "随 360 安全卫士卸载"},
    {"hipstray.exe", "安全防护", "火绒安全托盘进程", "轻量，占用低"},
    {"hipsdaemon.exe", "安全防护", "火绒安全主服务", "轻量，占用低"},

    // ---- 浏览器 ----
    {"msedge.exe", "浏览器", "Microsoft Edge 主进程", "不用可关闭；后台驻留可在设置里关掉「启动加速」"},
    {"msedgewebview2.exe", "浏览器", "Edge WebView2 运行时宿主", "被其他应用内嵌使用，数量多属正常"},
    {"chrome.exe", "浏览器", "Google Chrome 主进程", "不用可关闭；后台驻留可在设置里关掉"},
    {"firefox.exe", "浏览器", "Mozilla Firefox 主进程", "不用可关闭"},

    // ---- 云同步 ----
    {"onedrive.exe", "云同步", "微软 OneDrive 同步客户端", "不用于可退出并取消开机自启"},
    {"baidunetdisk.exe", "云同步", "百度网盘同步客户端", "不用于可退出并取消开机自启"},
    {"adrive.exe", "云同步", "阿里云盘客户端", "不用于可退出并取消开机自启"},
    {"dropbox.exe", "云同步", "Dropbox 同步客户端", "不用于可退出并取消开机自启"},
    {"wpscloudsvr.exe", "云同步", "WPS 云文档同步服务", "不用云文档可在 WPS 设置里关闭"},

    // ---- 通讯与社交 ----
    {"wechat.exe", "通讯与社交", "微信（PC 版）主进程", "不用可退出，常驻占用偏低"},
    {"weixin.exe", "通讯与社交", "微信新版客户端主进程", "不用可退出"},
    {"qq.exe", "通讯与社交", "QQ 主进程", "不用可退出"},
    {"tim.exe", "通讯与社交", "TIM 主进程", "不用可退出"},
    {"dingtalk.exe", "通讯与社交", "钉钉主进程", "不用可退出，建议关闭开机自启"},
    {"feishu.exe", "通讯与社交", "飞书主进程", "不用可退出，建议关闭开机自启"},
    {"wxwork.exe", "通讯与社交", "企业微信主进程", "不用可退出"},
    {"teams.exe", "通讯与社交", "Microsoft Teams 主进程", "不用可退出，建议关闭自动启动"},
    {"ms-teams.exe", "通讯与社交", "Microsoft Teams（新版）主进程", "不用可退出"},

    // ---- 开发工具 ----
    {"code.exe", "开发工具", "Visual Studio Code 主进程", "不用可退出；会派生多个扩展宿主子进程"},
    {"trae.exe", "开发工具", "Trae IDE 主进程", "不用可退出"},
    {"idea64.exe", "开发工具", "IntelliJ IDEA 主进程", "不用可退出；建议调低索引并发"},
    {"node.exe", "开发工具", "Node.js 运行时", "常由开发工具或脚本派生，确认用途后再结束"},
    {"python.exe", "开发工具", "Python 解释器", "常由脚本或工具派生，确认用途后再结束"},
    {"git.exe", "开发工具", "Git 命令行", "瞬时执行，一般不会常驻"},
    {"com.docker.backend.exe", "开发工具", "Docker Desktop 后端引擎", "不用 Docker 可在托盘退出"},
    {"docker desktop.exe", "开发工具", "Docker Desktop 主进程", "不用可退出并取消开机自启"},
    {"windowsterminal.exe", "开发工具", "Windows 终端", "不用可关闭"},

    // ---- 存储与索引 ----
    {"everything.exe", "存储与索引", "Everything 本地文件索引", "索引占用低，可改为按需启动"},
    {"searchindexer.exe", "存储与索引", "Windows 搜索索引器", "占用高时可停用 Windows Search 服务"},
    {"searchprotocolhost.exe", "存储与索引", "搜索协议宿主", "随搜索动作短暂出现"},
    {"searchfilterhost.exe", "存储与索引", "搜索筛选器宿主", "随搜索动作短暂出现"},

    // ---- 网络 ----
    {"thunder.exe", "网络", "迅雷下载客户端", "不用可退出；后台上传会占带宽"},

    // ---- 媒体与娱乐 ----
    {"potplayermini64.exe", "媒体与娱乐", "PotPlayer 播放器", "不用可关闭"},
    {"cloudmusic.exe", "媒体与娱乐", "网易云音乐", "不用可退出"},
    {"qqmusic.exe", "媒体与娱乐", "QQ 音乐", "不用可退出"},
    {"spotify.exe", "媒体与娱乐", "Spotify 客户端", "不用可退出"},

    // ---- 游戏 ----
    {"steam.exe", "游戏", "Steam 客户端", "不用可退出，建议关闭开机自启"},
    {"gamebar.exe", "游戏", "Xbox Game Bar 覆盖层", "不用可在设置里关闭游戏栏"},
    {"gamebarpresencewriter.exe", "游戏", "游戏栏状态写入进程", "不用游戏栏可关闭，占用极低"},

    // ---- 办公 ----
    {"winword.exe", "办公", "Microsoft Word", "关闭文档后进程通常随即退出"},
    {"excel.exe", "办公", "Microsoft Excel", "关闭工作簿后进程通常随即退出"},
    {"powerpnt.exe", "办公", "Microsoft PowerPoint", "关闭文稿后进程通常随即退出"},
    {"outlook.exe", "办公", "Microsoft Outlook", "不用可退出，建议关闭开机自启"},
    {"wps.exe", "办公", "WPS Office 文字组件", "关闭文档后可能仍驻留，可在设置里关掉后台常驻"},
    {"et.exe", "办公", "WPS Office 表格组件", "关闭表格后可能仍驻留，可在设置里关掉后台常驻"},
    {"wpp.exe", "办公", "WPS Office 演示组件", "关闭演示后可能仍驻留，可在设置里关掉后台常驻"},

    // ---- 驱动与固件 ----
    {"nvcontainer.exe", "驱动与固件", "NVIDIA 驱动容器进程", "随驱动安装，占用低；可关掉 GeForce Experience"},
    {"nvdisplay.container.exe", "驱动与固件", "NVIDIA 显示驱动容器", "不建议结束"},
    {"radeonsoftware.exe", "驱动与固件", "AMD Radeon 软件主进程", "不用可在驱动安装时选最小安装"},
    {"atiesrxx.exe", "驱动与固件", "AMD 外部事件服务", "显卡驱动组件，不建议结束"},
    {"igfxem.exe", "驱动与固件", "Intel 显卡事件管理器", "显卡驱动组件，不建议结束"},
    {"rtkauduservice64.exe", "驱动与固件", "Realtek 音频驱动服务宿主", "声卡驱动组件，不建议结束"},
};

/*
 * ========================================================================
 * 数据：服务内置规则
 * ========================================================================
 * match 键为 SCM 服务名（小写），见任务管理器「服务」标签的「名称」列
 */
const BuiltinRule kServiceRules[] = {
    // ---- 更新与维护 ----
    {"wuauserv", "更新与维护", "Windows 更新", "保持启用；空闲时自动扫描属正常"},
    {"bits", "更新与维护", "后台智能传输，负责更新与商店下载", "保持启用；占用高通常正在下载"},
    {"dosvc", "更新与维护", "传递优化，P2P 分享更新包", "可禁用以节省上传带宽"},
    {"usosvc", "更新与维护", "更新 Orchestrator，调度更新流程", "保持启用"},
    {"trustedinstaller", "更新与维护", "Windows 模块安装程序", "仅安装更新时活跃，不建议禁用"},

    // ---- 存储与索引 ----
    {"wsearch", "存储与索引", "Windows Search 索引服务", "占用高可停用，停用后开始菜单搜索变慢"},
    {"storsvc", "存储与索引", "存储服务，管理磁盘与存储设置", "保持启用"},
    {"vss", "存储与索引", "卷影复制，供备份与还原点使用", "不用还原点可停用"},

    // ---- 系统核心 ----
    {"sysmain", "系统核心", "SysMain（原 Superfetch），预读与内存预取", "机械硬盘上有用，纯 SSD 可停用"},
    {"eventlog", "系统核心", "Windows 事件日志", "不可禁用"},
    {"schedule", "系统核心", "任务计划程序", "不可禁用"},
    {"plugplay", "系统核心", "即插即用", "不可禁用"},
    {"winmgmt", "系统核心", "WMI 服务", "不建议禁用，多数管理软件依赖它"},
    {"w32time", "系统核心", "Windows 时间同步", "保持启用，开销可忽略"},
    {"audiosrv", "系统核心", "Windows Audio 音频服务", "禁用后无声"},
    {"audioendpointbuilder", "系统核心", "音频端点生成器", "不建议禁用"},
    {"samss", "系统核心", "安全账户管理器", "不可禁用"},
    {"seclogon", "系统核心", "辅助登录服务", "一般无需手动管理"},
    {"gpsvc", "系统核心", "组策略客户端", "保持启用"},
    {"sppsvc", "系统核心", "软件保护平台，负责激活与许可", "按需启动，不要禁用"},
    {"wlidsvc", "系统核心", "Microsoft 账户登录助手", "用微软账户登录时保持启用"},
    {"shellhwdetection", "系统核心", "硬件检测与自动播放", "可停用，减少设备接入时的弹窗"},
    {"sens", "系统核心", "系统事件通知服务", "保持启用"},

    // ---- 桌面外壳 ----
    {"themes", "桌面外壳", "主题与视觉样式", "不建议禁用"},

    // ---- 网络 ----
    {"dnscache", "网络", "DNS 客户端缓存", "保持启用，可加快域名解析"},
    {"dhcp", "网络", "DHCP 客户端，自动获取 IP", "保持启用"},
    {"nsi", "网络", "网络存储接口", "不建议禁用"},
    {"nlasvc", "网络", "网络位置感知", "保持启用"},
    {"wcmsvc", "网络", "Windows 连接管理器", "保持启用"},
    {"bfe", "网络", "基本筛选引擎，防火墙的依赖项", "不可禁用"},
    {"wlansvc", "网络", "WLAN 自动配置", "用无线网时保持启用"},
    {"dot3svc", "网络", "有线自动配置", "用有线网时保持启用"},
    {"iphlpsvc", "网络", "IP 助手，IPv6 隧道与转换", "一般保持启用"},
    {"lanmanserver", "网络", "Server 服务，提供文件与打印共享", "不用共享可停用，减少暴露面"},
    {"lanmanworkstation", "网络", "Workstation 服务，访问网络共享", "一般保持启用"},
    {"upnphost", "网络", "UPnP 设备主机", "不用 UPnP 可停用，减少暴露面"},
    {"ssdpsrv", "网络", "SSDP 发现服务", "不用可停用"},
    {"fdphost", "网络", "功能发现提供程序主机", "随发现功能按需启动"},
    {"fdrespub", "网络", "功能发现资源发布", "不用可停用"},
    {"termservice", "网络", "远程桌面服务", "不用远程桌面可停用，减少暴露面"},

    // ---- 安全防护 ----
    {"windefend", "安全防护", "Microsoft Defender 防病毒", "不建议停用"},
    {"securityhealthservice", "安全防护", "Windows 安全中心服务", "保持启用"},
    {"wscsvc", "安全防护", "安全中心，监控防护软件状态", "保持启用"},
    {"mpssvc", "安全防护", "Windows Defender 防火墙", "不建议停用"},
    {"cryptsvc", "安全防护", "加密服务，负责证书与目录签名验证", "保持启用"},

    // ---- 系统服务 ----
    {"appxsvc", "系统服务", "AppX 部署服务，负责商店应用安装", "按需启动，不建议禁用"},
    {"clipsvc", "系统服务", "客户端许可服务", "一般无需手动管理"},
    {"wpnservice", "系统服务", "Windows 推送通知", "保持启用，否则收不到通知"},
    {"diagtrack", "系统服务", "连接用户体验和遥测", "可停用，减少遥测上报"},
    {"dmwappushservice", "系统服务", "WAP 推送消息路由", "可停用"},
    {"wersvc", "系统服务", "Windows 错误报告", "可停用，减少崩溃上报"},
    {"pcasvc", "系统服务", "程序兼容性助手", "可停用，减少后台检查"},
    {"dps", "系统服务", "诊断策略服务", "可停用，节省少量资源"},
    {"spooler", "系统服务", "打印后台处理程序", "不用打印机可停用，该服务历史漏洞较多"},
    {"lfsvc", "系统服务", "地理位置服务", "不用定位可停用"},
    {"mapsbroker", "系统服务", "下载的地图管理", "不用地图可停用"},
    {"cdpsvc", "系统服务", "连接设备平台服务，支撑人脉与共享体验", "可停用"},
    {"cdpusersvc*", "系统服务", "连接设备平台用户服务（每用户实例）", "可停用"},
    {"onesyncsvc*", "云同步", "同步宿主服务（每用户实例），支撑邮件与 OneDrive 等同步", "不用于可停用"},

    // ---- 驱动与固件 ----
    {"bthserv", "驱动与固件", "蓝牙支持服务", "不用蓝牙可停用"},
    {"btagent", "驱动与固件", "蓝牙音频网关代理", "不用蓝牙音频可停用"},
};

}  // namespace

const BuiltinRule* builtin_process_rules() { return kProcessRules; }

int builtin_process_rule_count() {
    return static_cast<int>(sizeof(kProcessRules) / sizeof(kProcessRules[0]));
}

const BuiltinRule* builtin_service_rules() { return kServiceRules; }

int builtin_service_rule_count() {
    return static_cast<int>(sizeof(kServiceRules) / sizeof(kServiceRules[0]));
}

const BuiltinRule* find_builtin_rule(const BuiltinRule* rules, int count, const std::string& name) {
    /*
     * ========================================================================
     * 步骤1：空键直接返回
     * ========================================================================
     */
    if (rules == nullptr || count <= 0) {
        return nullptr;
    }
    const std::string key = to_lower_ascii(name);
    if (key.empty()) {
        return nullptr;
    }

    /*
     * ========================================================================
     * 步骤2：两遍扫表 —— 先精确，再前缀
     * ========================================================================
     * 说明：分两遍是为了让精确规则永远胜过前缀规则，不受表内书写顺序影响
     */
    for (int i = 0; i < count; ++i) {
        const char* match = rules[i].match;
        const std::size_t len = std::strlen(match);
        if (len == 0 || match[len - 1] == '*') {
            continue;
        }
        if (key == match) {
            return &rules[i];
        }
    }

    for (int i = 0; i < count; ++i) {
        const char* match = rules[i].match;
        const std::size_t len = std::strlen(match);
        if (len == 0 || match[len - 1] != '*') {
            continue;
        }
        const std::size_t prefix_len = len - 1;
        if (key.size() >= prefix_len && key.compare(0, prefix_len, match, prefix_len) == 0) {
            return &rules[i];
        }
    }

    return nullptr;
}

}  // namespace perfstat::analyze
