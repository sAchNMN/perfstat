/*
 * ========================================================================
 * 模块：collect / startup_collector
 * ========================================================================
 * 作用：枚举开机自启项 + 解析启用状态 + 抽出目标可执行文件路径
 * 依据：docs/DESIGN.md 6.6 / 12
 */
#include "collect/startup_collector.h"

#include <objbase.h>
#include <shlguid.h>
#include <shlobj.h>

#include <cstddef>
#include <string>

namespace perfstat {
namespace {

// Run 键的相对路径（HKCU / HKLM 通用）
constexpr const wchar_t* kRunPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

/*
 * StartupApproved 作用域标签
 * 说明：禁用状态必须与「哪一处 Run 键」一一对应，不能跨作用域按名匹配
 */
constexpr const char* kTagHcRun = "hc-run";        // HKCU Run
constexpr const char* kTagHlRun64 = "hl-run64";    // HKLM Run 64 位视图
constexpr const char* kTagHlRun32 = "hl-run32";    // HKLM Run 32 位视图
constexpr const char* kTagHcFolder = "hc-folder";  // 用户启动文件夹
constexpr const char* kTagHlFolder = "hl-folder";  // 公共启动文件夹

// 组合键：作用域 + 分隔符 + 小写名
std::string scoped_key(const std::string& tag, const std::string& name) {
    std::string key = tag;
    key.push_back('\x1f');
    key += name;
    return key;
}

// 宽字符转 UTF-8
std::string wide_to_utf8(const wchar_t* w) {
    if (w == nullptr || *w == L'\0') {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

// UTF-8 转宽字符
std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

// ASCII 小写（键名比对用；注册表键名不区分大小写）
std::string to_lower_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) {
        ++b;
    }
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) {
        --e;
    }
    return s.substr(b, e - b);
}

// 展开 %VAR%；无法识别的变量原样保留
std::string expand_env(const std::string& s) {
    if (s.find('%') == std::string::npos) {
        return s;
    }
    const std::wstring w = utf8_to_wide(s);
    const DWORD need = ExpandEnvironmentStringsW(w.c_str(), nullptr, 0);
    if (need == 0) {
        return s;
    }
    std::wstring buf(need, L'\0');
    const DWORD got = ExpandEnvironmentStringsW(w.c_str(), buf.data(), need);
    if (got == 0 || got > need) {
        return s;
    }
    buf.resize(got - 1);
    return wide_to_utf8(buf.c_str());
}

/*
 * 从命令行抽出可执行文件路径
 * 规则：
 *   1) 以引号开头 -> 取引号内（覆盖 "C:\Program Files\a b\app.exe" -arg）
 *   2) 否则以 ".exe" 结尾处截断（覆盖未加引号但路径含空格的情况）
 *   3) 再退化为取到第一个空格
 */
std::string extract_exe_path(const std::string& raw) {
    const std::string s = trim(raw);
    if (s.empty()) {
        return {};
    }
    if (s.front() == '"') {
        const std::size_t end = s.find('"', 1);
        return (end != std::string::npos && end > 1) ? s.substr(1, end - 1) : std::string();
    }
    const std::string lower = to_lower_ascii(s);
    const std::size_t pos = lower.find(".exe");
    if (pos != std::string::npos) {
        return s.substr(0, pos + 4);
    }
    const std::size_t sp = s.find(' ');
    return sp == std::string::npos ? s : s.substr(0, sp);
}

// 用 IShellLink 解析 .lnk 的目标与参数
bool resolve_lnk(const std::wstring& path, std::string* target, std::string* args) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                reinterpret_cast<void**>(&link)))) {
        return false;
    }

    bool ok = false;
    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
        if (SUCCEEDED(file->Load(path.c_str(), STGM_READ))) {
            wchar_t buf[1024]{};
            if (SUCCEEDED(link->GetPath(buf, 1024, nullptr, SLGP_UNCPRIORITY)) && buf[0] != L'\0') {
                *target = wide_to_utf8(buf);
                wchar_t abuf[1024]{};
                if (SUCCEEDED(link->GetArguments(abuf, 1024))) {
                    *args = trim(wide_to_utf8(abuf));
                }
                ok = true;
            }
        }
        file->Release();
    }
    link->Release();
    return ok;
}

}  // namespace

void StartupCollector::read_approved_key(HKEY hive, REGSAM view, bool is_folder,
                                         const std::string& tag) {
    /*
     * ========================================================================
     * 步骤：读 StartupApproved，收集「被禁用」的值名
     * ========================================================================
     * 数据：REG_BINARY，首字节最低位为 1 表示禁用（0x03 / 0x07），
     *       为 0 表示启用（0x02 / 0x06）——与任务管理器「启动应用」页一致
     * 说明：收进 disabled_ 时带作用域前缀，避免跨 hive / 视图互相污染
     */
    const wchar_t* sub =
        is_folder
            ? L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder"
            : L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

    HKEY key = nullptr;
    if (RegOpenKeyExW(hive, sub, 0, KEY_READ | view, &key) != ERROR_SUCCESS) {
        return;
    }

    wchar_t name[512]{};
    unsigned char data[64]{};
    for (DWORD index = 0;; ++index) {
        DWORD name_len = sizeof(name) / sizeof(wchar_t);
        DWORD data_len = sizeof(data);
        DWORD type = 0;
        const LONG rc = RegEnumValueW(key, index, name, &name_len, nullptr, &type, data, &data_len);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS) {
            continue;
        }
        if (type != REG_BINARY || data_len < 1) {
            continue;
        }
        if ((data[0] & 0x01) != 0) {
            const std::string value_name =
                to_lower_ascii(wide_to_utf8(std::wstring(name, name_len).c_str()));
            disabled_.insert(scoped_key(tag, value_name));
        }
    }
    RegCloseKey(key);
}

void StartupCollector::read_run_key(HKEY hive, REGSAM view, const std::string& tag,
                                    const std::string& source_label, const std::string& scope,
                                    StartupInfoList& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(hive, kRunPath, 0, KEY_READ | view, &key) != ERROR_SUCCESS) {
        return;
    }

    wchar_t name[512]{};
    unsigned char data[8192]{};
    for (DWORD index = 0;; ++index) {
        DWORD name_len = sizeof(name) / sizeof(wchar_t);
        DWORD data_len = sizeof(data);
        DWORD type = 0;
        const LONG rc = RegEnumValueW(key, index, name, &name_len, nullptr, &type, data, &data_len);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
            continue;
        }

        // 值数据转 UTF-8（data_len 含结尾 NUL，按需剔除）
        std::size_t chars = data_len / sizeof(wchar_t);
        if (chars > 0 && reinterpret_cast<const wchar_t*>(data)[chars - 1] == L'\0') {
            --chars;
        }
        const std::string raw_command =
            wide_to_utf8(std::wstring(reinterpret_cast<const wchar_t*>(data), chars).c_str());

        // 值名：Windows 约定用一对引号包裹表示「原样显示」
        std::string value_name = wide_to_utf8(std::wstring(name, name_len).c_str());
        std::string display = value_name;
        if (display.size() >= 2 && display.front() == '"' && display.back() == '"') {
            display = display.substr(1, display.size() - 2);
        }
        if (display.empty()) {
            display = "(默认)";
        }

        StartupInfo info;
        info.name = display;
        info.command = raw_command;
        info.exe_path = extract_exe_path(expand_env(raw_command));
        info.source = source_label;
        info.scope = scope;
        info.kind = "registry";
        info.enabled = disabled_.count(scoped_key(tag, to_lower_ascii(display))) == 0 &&
                       disabled_.count(scoped_key(tag, to_lower_ascii(value_name))) == 0;
        out.push_back(std::move(info));
    }
    RegCloseKey(key);
}

void StartupCollector::read_startup_folder(const std::string& env_name, const std::string& tag,
                                           const std::string& source_label,
                                           const std::string& scope, StartupInfoList& out) {
    const std::wstring pattern = L"%" + utf8_to_wide(env_name) +
                                 L"%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\*";
    wchar_t expanded[1024]{};
    const DWORD n = ExpandEnvironmentStringsW(pattern.c_str(), expanded, 1024);
    if (n == 0 || n > 1024) {
        return;
    }

    const std::wstring dir = expanded;
    const std::size_t slash = dir.find_last_of(L'\\');
    const std::wstring base = (slash == std::wstring::npos) ? std::wstring() : dir.substr(0, slash + 1);

    WIN32_FIND_DATAW fd{};
    HANDLE handle = FindFirstFileW(dir.c_str(), &fd);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        // 跳过 desktop.ini 这类系统元数据文件（带 SYSTEM 属性），它不是自启项
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0) {
            continue;
        }
        const std::string file_utf8 = wide_to_utf8(fd.cFileName);
        const std::string lower = to_lower_ascii(file_utf8);
        if (lower == "desktop.ini") {
            continue;
        }

        const bool is_lnk = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".lnk") == 0;
        const bool is_url = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".url") == 0;

        StartupInfo info;
        info.name = (is_lnk || is_url) ? file_utf8.substr(0, file_utf8.size() - 4) : file_utf8;
        info.source = source_label;
        info.scope = scope;
        info.kind = "folder";
        // Explorer 用「带扩展名的文件名」作 StartupApproved 的值名，两者都试
        info.enabled = disabled_.count(scoped_key(tag, lower)) == 0 &&
                       disabled_.count(scoped_key(tag, to_lower_ascii(info.name))) == 0;

        if (is_lnk) {
            std::string target;
            std::string args;
            if (resolve_lnk(base + fd.cFileName, &target, &args)) {
                info.command = args.empty() ? target : target + " " + args;
                info.exe_path = extract_exe_path(target);
            }
        } else if (!is_url) {
            // 直接放在启动文件夹里的可执行文件
            info.command = wide_to_utf8(base.c_str()) + file_utf8;
            info.exe_path = extract_exe_path(info.command);
        }
        // .url 指向网页，没有本地可执行文件，command/exe_path 留空

        out.push_back(std::move(info));
    } while (FindNextFileW(handle, &fd));
    FindClose(handle);
}

StartupInfoList StartupCollector::collect() {
    /*
     * ========================================================================
     * 步骤1：重置并读「被禁用」名单
     * ========================================================================
     * 说明：先读 StartupApproved，后面的条目才能拿到正确的启用态
     */
    disabled_.clear();
    // HKCU 的 Run 键不分视图，故其 StartupApproved 两个视图都读、合并进同一作用域
    read_approved_key(HKEY_CURRENT_USER, KEY_WOW64_64KEY, false, kTagHcRun);
    read_approved_key(HKEY_CURRENT_USER, KEY_WOW64_32KEY, false, kTagHcRun);
    read_approved_key(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, false, kTagHlRun64);
    read_approved_key(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, false, kTagHlRun32);
    read_approved_key(HKEY_CURRENT_USER, KEY_WOW64_64KEY, true, kTagHcFolder);
    read_approved_key(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, true, kTagHlFolder);
    read_approved_key(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, true, kTagHlFolder);

    /*
     * ========================================================================
     * 步骤2：解析 .lnk 需要 COM，本函数内平衡地初始化 / 反初始化
     * ========================================================================
     * 说明：RPC_E_CHANGED_MODE 表示当前线程已被别人初始化，此时不再自行反初始化
     */
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool own_com = SUCCEEDED(hr);

    /*
     * ========================================================================
     * 步骤3：枚举五处来源
     * ========================================================================
     */
    StartupInfoList out;
    read_run_key(HKEY_CURRENT_USER, KEY_WOW64_64KEY, kTagHcRun, "HKCU Run", "user", out);
    read_run_key(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, kTagHlRun64, "HKLM Run（64 位）", "machine",
                 out);
    read_run_key(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, kTagHlRun32, "HKLM Run（32 位）", "machine",
                 out);
    read_startup_folder("APPDATA", kTagHcFolder, "启动文件夹（用户）", "user", out);
    read_startup_folder("ProgramData", kTagHlFolder, "启动文件夹（公共）", "machine", out);

    if (own_com) {
        CoUninitialize();
    }
    return out;
}

}  // namespace perfstat
