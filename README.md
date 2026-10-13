# PerfStat

Windows 11 上的**本地后台资源监控小工具**：常驻采集进程 / 服务 / 开机自启项的资源占用，提供一个本地 Web 界面查看实时排行、进程树、历史趋势与用途识别结果。

- **纯本地**：不联网、不上传、不需要账号。唯一的网络行为是本机 `127.0.0.1` 上的前后端通信。
- **零包管理**：后端第三方库只有 cpp-httplib、nlohmann/json、SQLite 三个单文件/amalgamation，直接放在 `src/third_party/`；前端不引图表库（曲线是手写 SVG）。
- **只读 + 不提权**：采集所需的接口（Toolhelp32 / SCM / 注册表 / PSAPI）在普通用户权限下即可读到；遇到提权进程读不到占用时，界面明确标「无权限」，而不是显示成 `0`。

## 当前进度

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| M1 | 采集骨架（进程 CPU / 内存） | 已完成 |
| M2 | 存储层（1 分钟聚合落库 + 30 天保留清理） | 已完成 |
| M3 | HTTP + SSE + 进程实时页面 | 已完成 |
| M4 | 服务线（SCM 枚举 + 三态占用 + 共享宿主标注） | 已完成 |
| M5 | 规则库（内置规则命中 + 人工覆盖） | 已完成 |
| ~~M6~~ | ~~联网识别~~ | 已取消（项目定位为纯本地工具） |
| M7 | 趋势图/历史页 + 进程树 + 启动项 | 已完成 |
| ~~M8~~ | ~~打包（单文件 exe + 托盘 + 自启）~~ | 已取消（本地自用，直接运行 exe 即可） |
| M9 | WebView2 壳（可选） | 未开始 |

## 功能

| 页面 | 内容 |
|------|------|
| 进程 | 实时排行（2s 一帧 SSE 推送）；**表格 ⇄ 进程树**切换，可按父子层级折叠；用途列可点击编辑 |
| 服务 | 服务列表 + 共享宿主标注 + 占用三态（有值 / 无权限 / 不适用） |
| 启动项 | 注册表 Run ×3 + 启动文件夹 ×2；含启用状态、解析出的目标程序与当前占用；标出「已禁用却在跑」 |
| 历史 | 1 小时 / 6 小时 / 24 小时 / 7 天四档区间；CPU、内存、单实体三条趋势曲线 + 区间汇总榜 |

**用途识别**（M5）为两级本地查找：人工覆盖（`dim_usage`，最高优先级）→ 内置规则表（编译进 exe，进程 89 条 + 服务 61 条）→ 未识别时前端可直接补齐。人工覆盖落库后跨重启生效，且写库时在 SQL 层带 `WHERE source <> 'manual'` 保护，任何自动回写都无法污染它。

## 构建

环境要求（Windows）：

- MinGW-w64 g++（需支持 C++20；实测 16.2.0）
- CMake ≥ 3.25、Ninja
- Node.js + npm（仅当前端有改动时需要）
- 目标平台 `_WIN32_WINNT=0x0A00`（cpp-httplib 明确拒绝低于 Win10 的目标）

```bash
# 1) 后端：配置 + 编译（产出 build/perfstat.exe）
cmake --preset mingw-release
cmake --build build

# 调试版（产出 build-debug/perfstat.exe）
cmake --preset mingw-debug
cmake --build build-debug
```

前端产物由 `cmake/EmbedFrontend.cmake` 在**配置阶段**嵌入 exe。若改了前端，先构建前端再重新配置：

```bash
cd frontend
npm install
npm run build      # 产出 frontend/dist/
cd ..
cmake --preset mingw-release   # 重新配置以嵌入新的 dist/
cmake --build build
```

前端开发期免重新编译后端，用 vite 代理直连：

```bash
cd frontend
npm run dev        # http://127.0.0.1:5173，/api 代理到 127.0.0.1:17321
```

## 运行

```bash
# 常驻后端：采样 + 落库 + HTTP/SSE（浏览器打开提示的地址）
./build/perfstat.exe serve
./build/perfstat.exe serve --no-token          # 关闭 token 鉴权（仅本机调试）
./build/perfstat.exe serve --no-persist        # 只服务实时视图，不落库

# 采集落库一段时间（用于验证 / 造历史数据）
./build/perfstat.exe record --interval=1000 --duration=150

# 数据库概况与保留清理
./build/perfstat.exe db --stats
./build/perfstat.exe db --purge --days=30

# 采集骨架（控制台打印）
./build/perfstat.exe collect

# 无参运行 = 托盘常驻（M8 待接入）
./build/perfstat.exe
```

数据落在 `%LOCALAPPDATA%\PerfStat\`（`perfstat.db` / `config.json` / `logs\`）。

## 目录结构

```
s/
├─ CMakeLists.txt / CMakePresets.json
├─ cmake/EmbedFrontend.cmake         # 前端产物 → C 数组
├─ src/
│  ├─ main.cpp                       # 入口：无参=托盘，带参=子命令分发
│  ├─ app/                           # *_command / paths
│  ├─ collect/                       # process / service / startup 采集 + sampler
│  ├─ store/                         # db / aggregator / persist_worker
│  ├─ analyze/                       # usage_types / builtin_rules / rule_db
│  ├─ api/                           # server / sse_hub / *_store / serialize
│  └─ third_party/                   # httplib.h / json.hpp / sqlite3.c,h
├─ frontend/                         # Vue 3 + Vite
│  └─ src/{views,components,api,router}
└─ docs/DESIGN.md                    # 设计共识（决策表、数据模型、API 契约、进度与验收证据）
```

## 设计文档

`docs/DESIGN.md` 是本项目的唯一设计共识，包含：环境事实、决策总表（D1–D17）、整体架构、数据模型 DDL、API 契约、里程碑定义，以及每个里程碑的**实测验收证据**与「与设计的偏差（有意为之）」。改动实现前建议先读它。

## 已知限制

- 只支持 Windows（强依赖 Toolhelp32 / SCM / PSAPI / 注册表）。
- 提权进程的占用读不到，界面按三态标为「无权限」而非给假数值。
- 服务、启动项的占用是**宿主 / 命中进程级**的数字，无法做到 per-service 或 per-item 归因。
- 启动项页为**只读**，不提供启用/禁用开关（如需修改请用系统任务管理器）。
- 不提供安装包 / 单文件打包：本项目为本地自用，编译后直接运行 `perfstat.exe` 即可。

## 许可证

本项目采用 **MIT License**，详见 [LICENSE](LICENSE)。

## 第三方许可

`src/third_party/` 下的三个库各自遵循其原始许可：cpp-httplib（MIT）、nlohmann/json（MIT）、SQLite（Public Domain）。
