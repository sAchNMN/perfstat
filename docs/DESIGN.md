# 系统性能统计小工具 — 设计共识

> 本文档由 grill-me 流程逐轮收敛而来，每一节都对应一次已拍板的决策。
> 状态：**待用户最终确认**。确认后才进入编码。

---

## 1. 目标与范围

Windows 11 上的后台资源监控小工具。C++ 写后端采集与存储，Web 做前端展示。

对应原始需求六项：

| # | 需求 | 归属模块 |
|---|------|---------|
| 1 | 按指定时间段统计汇总后台应用性能占用 | store/aggregator + /api/stats + /history |
| 2 | 实时查看各后台应用资源占用 | collect/sampler + SSE + / |
| 3 | 读取并列出所有后台服务及占用 | collect/service_collector + /services |
| 4 | 自动分析判断每个服务的用途 | analyze/rule_db |
| 5 | 基于占用数据给出针对性清理建议 | analyze/rule_db + /api/usage |
| 6 | 预留扩展功能 | 趋势图 / 进程树 / 启动项管理 |

**明确不做**：多机上报汇总、网络 IO 按进程统计（需 ETW）、跨平台、外部 API 调用（用途识别只用本地规则库，全程不联网）。

---

## 2. 环境事实（实测）

| 项 | 状态 |
|----|------|
| C++ 编译器 | MinGW-w64 g++ 16.2.0（PATH 无 MSVC、无 vcpkg） |
| 构建 | CMake 4.4.2 + Ninja |
| 前端运行时 | Node 22.22.2 + npm |
| WebView2 Runtime | 已装 154.0.4258.62 |
| 边际约束 | 日常运行无管理员权限 |

---

## 3. 决策总表

| 编号 | 决策点 | 结论 |
|------|--------|------|
| D1 | 监控范围 | 进程线 + 服务线，前端分 Tab；先做进程线，服务线复用采集骨架 |
| D2 | 权限模型 | 默认只读；危险动作 spawn 提权 helper，UAC 只在动作时刻弹 |
| D3 | 交付形态 | 先浏览器访问 localhost；后期加 WebView2 壳，不返工 |
| D4 | C++ 技术栈 | cpp-httplib + nlohmann/json + SQLite3 amalgamation，全 header/单文件，零包管理 |
| D5 | 前端栈 | Vue3 + Vite，构建产物嵌入 exe |
| D6 | 采集路线 | Toolhelp32 快照 + GetProcessTimes / GetProcessMemoryInfo / GetProcessIoCounters，自算 delta。服务侧走 SCM |
| D7 | 统计口径 | CPU = 占整机百分比（按逻辑核归一），存 平均 + 峰值；内存 = 平均/峰值工作集；IO = 读写字节增量 |
| D8 | 采样与保留 | 2s 采样；内存环形缓冲留 5 分钟；落盘 1 分钟聚合行；保留 30 天 |
| D9 | 后端形态 | 托盘常驻，不做 Windows 服务；自启走注册表 Run 项或任务计划 |
| D10 | 「后台」判定 | 无前台可见顶层窗口 ∨ 属于服务宿主；默认屏蔽系统关键进程；前端给「显示系统进程」开关 |
| D11 | 共享宿主 | 不摊。同宿主服务统一显示宿主真实占用 + 标注「N 个服务共享」+ 可展开 |
| D12 | 进程标识 | 实例键 = 进程名 + 完整路径 + 启动时间；聚合视图键 = 进程名 + 路径 |
| D13 | 规则库 | builtin 内置 + manual 人工覆盖，manual 优先级最高；内置规则随 exe 发版更新，不落库 |
| D14 | 实时推送 | SSE，非轮询非 WebSocket |
| D15 | 分发 | 单文件 exe；数据落 `%LOCALAPPDATA%\PerfStat\` |
| D16 | 轻量化 | 内存 ≤ 40MB，CPU 均值 ≤ 0.3% / 峰值 ≤ 2%；无前端连接时自动降频 |
| D17 | 服务占用可见性 | SCM 元数据永远完整（名称/显示名/状态/启动类型，零提权依赖）；占用列三态：有值(宿主 PID 可读) / 无权限(宿主 PID 被拒，hover 提示需提权) / 不适用(已停止或无宿主)；共享宿主按 D11 显示宿主聚合占用。提权仅能把「无权限」转为「宿主级聚合」，无法 per-service 归因 |

---

## 4. 整体架构

```
┌──────────────────────────────────────────────────────────┐
│  浏览器 / 后期 WebView2 壳                                │
│  Vue3 + Vite 单页应用，六个页面                            │
└───────────────┬──────────────────────────────────────────┘
                │  HTTP/1.1  ↕  REST + SSE（127.0.0.1，带 token）
┌───────────────▼──────────────────────────────────────────┐
│  C++ 后端（单进程，托盘常驻）                              │
│                                                          │
│  api/        路由、SSE Hub、鉴权                          │
│  analyze/    规则库（内置规则 + 人工覆盖）                │
│  store/      SQLite、1 分钟聚合、保留清理（写库独立线程）   │
│  collect/    进程采集、服务采集、Waitable Timer 调度、环形缓冲│
│  app/        托盘、配置、路径解析                          │
│  action/     提权 helper（同 exe 第二子命令，按需 spawn）   │
└───────────────┬──────────────────────────────────────────┘
                │  Win32 API
┌───────────────▼──────────────────────────────────────────┐
│  Toolhelp32 / GetProcessTimes / GetProcessMemoryInfo     │
│  GetProcessIoCounters / EnumServicesStatusEx / 注册表     │
└──────────────────────────────────────────────────────────┘
```

**分层原则**：采集线程只做枚举和计算，绝不碰磁盘；写库收在独立线程；HTTP 线程只读内存与 SQLite。三条线程互不阻塞。

---

## 5. 前后端通信

两条通道，职责分开。

| 通道 | 用途 | 实现 |
|------|------|------|
| REST | 查询、配置、动作 | cpp-httplib 同步 handler |
| SSE | 实时推送 | `/api/realtime` 长连接，2s 一帧，chunked |

**SSE 帧格式**：每帧一条 JSON 数组，元素为当前轮次的实体快照（含 entity_id、cpu、mem、io 增量）。前端用 `EventSource` 接收，直接驱动表格。

**连接数即采样节拍**：有 SSE 订阅者时采样间隔 2s；无订阅者时降到 5s 且暂停落盘聚合。这是 D16 轻量化的主要手段。

**鉴权**：绑定 `127.0.0.1`；启动时生成 32 字节随机 token 写入进程环境，打开浏览器时带 `?t=<token>`，前端存入 sessionStorage，所有 `/api/*` 请求带 `X-Token`。防止本机其他进程滥用 `/api/actions/*`。

**端口**：默认 17321，被占用则自动 +1，并把最终端口写入启动 URL。

---

## 6. 数据采集与分析方法

### 6.1 进程采集（2s 一轮）

1. `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` 枚举全部 PID（顺带取 `th32ParentProcessID`，供进程树用）
2. 对每个 PID：`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` 拿句柄
3. `GetProcessTimes` → 内核态 + 用户态累计时间 → 与上一轮差分 → CPU 占用
4. `GetProcessMemoryInfo` → 工作集
5. `GetProcessIoCounters` → 读写字节累计 → 与上一轮差分 → 区间增量
6. `QueryFullProcessImageName` → 完整路径
7. `GetProcessTimes` 的创建时间 + 路径 + 进程名 → 拼出实例键（D12）

CPU 归一公式：

```
cpu_percent = (Δkernel + Δuser) / (Δwall × 逻辑核数) × 100
```

分母用逻辑核数让结果是「占整机百分比」，和任务管理器口径一致。

### 6.2 服务采集

`OpenSCManager` + `EnumServicesStatusEx` 枚举全部服务，取服务名、显示名、状态、启动类型、宿主 PID。再把宿主 PID 关联到进程采集结果，得到 D11 的「N 个服务共享」。**宿主占用按 D17 三态呈现**：宿主 PID 在进程采集中 `accessible` 为真才出值；被拒（M1 实测非提权下 108 PID，含 74 svchost）则标「无权限」；共享宿主本质是进程级数字，提权也无法 per-service 归因。

### 6.3 「后台」过滤

```
is_background =
    (该进程无可见顶层窗口)  ∨  (该 PID 是某个服务的宿主)
且
    进程名 ∉ 系统关键进程黑名单
```

黑名单：`System`、`Idle`、`Registry`、`Memory Compression`、`csrss.exe`、`wininit.exe`、`smss.exe`、`dwm.exe`、`services.exe`、`lsass.exe`。

前端「显示系统进程」开关可放行黑名单，但判定式本身不变。

### 6.4 存储与聚合

- 实时 5 分钟数据只进内存环形缓冲，不入库
- 写库线程每 60s 把这一分钟的 30 个采样点聚合为一行，单事务批量写入
- `fact_metric_1m` 一进程一分钟一行；300 进程 ≈ 43 万行/天 ≈ 1300 万行/月
- SQLite 开 WAL，`PRAGMA synchronous=NORMAL`
- 每日清理一次 `bucket_ts < now - 30d`

**环形缓冲内存占用**：150 点 × 32 字节 ≈ 4.7 KB/进程。300 进程 ≈ **1.4 MB**，500 进程 ≈ 2.4 MB。相对 40MB 预算占 3%~6%，不是问题。实现上用每实体一个定长 `std::array<SamplePoint,150>` + head 索引，零运行时分配；实体退出后槽位标记 stale，30 分钟无数据回收。

### 6.5 用途识别与清理建议

两级查找，逐级降级，全程本地，不产生任何网络请求：

```
1. 人工覆盖（dim_usage，source = 'manual'）
   命中 → 直接返回

2. 内置规则表（编译进 exe，进程 89 条 + 服务 61 条）
   命中 → 返回，source = 'builtin'

未命中 → { category: '其他', purpose: '未知' }
   前端按「未识别」渲染，用户可在界面上直接填写补齐
```

人工覆盖优先级最高，覆盖同键的内置结果；内置规则随 exe 发版更新，不落库。

### 6.6 启动项采集（约 30s 一轮）

开机自启项取自五处，全程只读、零提权：

| 来源 | 位置 | 范围 |
|------|------|------|
| HKCU Run | `HKCU\...\CurrentVersion\Run` | 当前用户 |
| HKLM Run（64 位） | `HKLM\...\CurrentVersion\Run`，`KEY_WOW64_64KEY` | 所有用户 |
| HKLM Run（32 位） | 同上键，`KEY_WOW64_32KEY` | 所有用户 |
| 启动文件夹（用户） | `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup` | 当前用户 |
| 启动文件夹（公共） | `%ProgramData%\...\Programs\Startup` | 所有用户 |

**启用状态**取自 `Explorer\StartupApproved\{Run,StartupFolder}`，与任务管理器「启动应用」页判定一致：值数据是 12 字节 REG_BINARY，首字节最低位为 **0（偶数，0x02/0x06）= 启用**、**1（奇数，0x03/0x07）= 禁用**。

**作用域必须分开**：`StartupApproved` 会残留「对应 Run 值早已被删除」的历史记录，若把 HKCU / HKLM 各视图揉成一个集合按名查找，一个 hive 的残留会误伤另一个 hive 的同名条目。实现上以 `作用域标签 + 名` 作键分桶。

**命令 → 可执行文件**：注册表值可能是 `REG_EXPAND_SZ`，先展开 `%ENV%`；再从命令串抽 exe 路径（带引号取引号内，否则以 `.exe` 结尾处截断，容忍路径中的空格）。启动文件夹里的 `.lnk` 用 `IShellLink` 解析目标（含参数）；带 `SYSTEM` 属性的元数据文件（如 `desktop.ini`）不是自启项，跳过。

**占用在序列化阶段现算**：清单缓存约 30s，但「当前是否在跑、占多少」用当轮的进程快照按 exe 路径匹配（路径不中再退化为按文件名匹配）——这样清单一改不必频繁重读注册表，占用又始终是最新一轮的值。

---

## 7. 数据模型

```sql
-- 实体维表
CREATE TABLE dim_entity (
    entity_id    INTEGER PRIMARY KEY,
    kind         TEXT    NOT NULL,   -- 'process' | 'service'
    name         TEXT    NOT NULL,   -- 进程名 / 服务名
    path         TEXT,               -- exe 完整路径（服务可空）
    publisher    TEXT,               -- 版本资源里的发布者
    instance_key TEXT    NOT NULL,   -- 进程: name|path|starttime ; 服务: 服务名
    first_seen   INTEGER NOT NULL,
    last_seen    INTEGER NOT NULL,
    UNIQUE(kind, instance_key)
);

-- 用途 / 建议库
CREATE TABLE dim_usage (
    entity_id  INTEGER PRIMARY KEY REFERENCES dim_entity(entity_id),
    category   TEXT,
    purpose    TEXT,
    suggestion TEXT,
    source     TEXT    NOT NULL,     -- 'builtin' | 'manual'
    confidence REAL,
    updated_at INTEGER NOT NULL
);

-- 1 分钟聚合事实表
CREATE TABLE fact_metric_1m (
    entity_id    INTEGER NOT NULL,
    bucket_ts    INTEGER NOT NULL,   -- 分钟对齐 unix ts
    cpu_avg      REAL,               -- 占整机 %
    cpu_max      REAL,
    mem_avg_kb   INTEGER,
    mem_max_kb   INTEGER,
    io_read_kb   INTEGER,            -- 区间增量
    io_write_kb  INTEGER,
    sample_count INTEGER,
    PRIMARY KEY (entity_id, bucket_ts)
);
CREATE INDEX idx_fact_ts ON fact_metric_1m(bucket_ts);

-- 服务 → 宿主映射
CREATE TABLE dim_service_host (
    service_name   TEXT PRIMARY KEY,
    display_name   TEXT,
    host_entity_id INTEGER REFERENCES dim_entity(entity_id),
    start_type     TEXT,             -- auto | manual | disabled
    state          TEXT
);
```

落盘位置：

```
%LOCALAPPDATA%\PerfStat\perfstat.db
%LOCALAPPDATA%\PerfStat\config.json
%LOCALAPPDATA%\PerfStat\logs\app-YYYYMMDD.log
```

---

## 8. API 契约

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/snapshot` | 首屏一次性快照 |
| GET | `/api/realtime` | SSE 流，2s 一帧 |
| GET | `/api/processes` | 进程列表（排序 / 过滤 / 分页） |
| GET | `/api/processes/{id}` | 单进程详情 + 趋势 |
| GET | `/api/services` | 服务列表（含共享宿主标注） |
| GET | `/api/services/{name}` | 单服务详情 + 宿主展开 |
| GET | `/api/stats?from&to&bucket&top&entity_id` | 时间段聚合：`from`/`to` 秒级时间戳（缺省最近 1 小时）、`bucket` 桶粒度秒（缺省 60）、`top` 汇总行上限（缺省 50）、`entity_id` 存在时只返回该实体序列 |
| GET | `/api/startup` | 启动项：注册表 Run ×3 + 启动文件夹 ×2，含启用状态、命令、解析出的 exe 与当前占用（占用按当轮进程快照现算） |
| POST | `/api/analyze` | 本地批量识别（内置规则 + 人工覆盖），返回命中统计与未识别清单 |
| GET | `/api/usage` | 列出全部人工覆盖条目 |
| POST | `/api/usage` | 人工覆盖标签（source = manual），实体以 body 里的 kind/name/path 定位 |
| POST | `/api/actions/terminate` | 结束进程（内部 spawn 提权 helper） |
| POST | `/api/actions/service` | 停 / 启服务（同上） |
| GET | `/api/settings` | 读配置 |
| POST | `/api/settings` | 写配置（采样参数等） |

---

## 10. 轻量化预算

| 指标 | 目标 |
|------|------|
| 常驻内存 | ≤ 40MB |
| CPU 均值 | ≤ 0.3% |
| CPU 峰值 | ≤ 2% |

实现要点：

1. 采样调度用 Waitable Timer，不用 `Sleep` 空转
2. 采集线程不做任何磁盘 IO
3. SQLite 开 WAL，每 60s 单事务批量提交
4. 环形缓冲定长预分配，零运行时分配
5. 无 SSE 订阅者时降频到 5s，并暂停落盘聚合
6. 进程句柄用完立即 `CloseHandle`

---

## 11. 目录结构

```
s/
├─ CMakeLists.txt
├─ cmake/
│  └─ EmbedFrontend.cmake          # vitE 产物 → C 数组
├─ src/
│  ├─ main.cpp                     # 入口：托盘模式 / 子命令分发
│  ├─ app/                         # tray / config / paths / *_command
│  ├─ collect/                     # process_collector / service_collector
│  │                               # startup_collector / sampler / ring_buffer
│  ├─ store/                       # db / aggregator / persist_worker
│  ├─ analyze/                     # usage_types / builtin_rules / rule_db
│  ├─ api/                         # server / sse_hub / snapshot_store
│  │                               # service_store / startup_store / serialize
│  ├─ action/                      # privileged_helper
│  └─ third_party/                 # httplib.h / json.hpp / sqlite3.c,h
├─ frontend/
│  ├─ package.json / vite.config.ts
│  └─ src/                         # views / components / api / stores
└─ docs/
   └─ DESIGN.md
```

---

## 12. 前端页面

| 路由 | 内容 |
|------|------|
| `/` | 总览：CPU / 内存双 Top 榜 + 系统资源总览 |
| `/processes` | 进程：表格 ⇄ 进程树切换，可展开趋势 |
| `/services` | 服务：列表 + 共享宿主标注与展开 |
| `/startup` | 启动项：注册表 Run + 启动文件夹，按占用排序 |
| `/history` | 历史：时间段查询 + 趋势曲线 |
| `/settings` | 设置：采样参数、日志 |

---

## 13. 里程碑

| # | 内容 | 验收标准 |
|---|------|---------|
| M1 | 采集骨架 | 控制台打印进程 CPU/内存，与任务管理器数值一致 |
| M2 | 存储层 | 1 分钟聚合落库，30 天清理生效 |
| M3 | HTTP + SSE + 最小页面 | 浏览器 `/processes` 实时刷新 |
| M4 | 服务线 | 服务列表(名称/显示名/状态/启动类型) + 共享宿主标注 + 占用列三态(D17) |
| M5 | 规则库 | builtin 命中 + 人工覆盖生效 |
| M7 | 扩展项 | 趋势图 / 进程树 / 启动项三个页面完成 |
| ~~M8~~ | ~~打包~~ | 已取消：本地自用，编译后直接运行 exe 即可，不做单文件打包 / 托盘 / 自启 |
| M9 | WebView2 壳 | 可选，不阻塞前八项 |

---

## 14. 未决项（编码中再定）

1. 提权 helper 的退出码约定与错误分类（`src/action/` 仍为骨架，`/api/actions/*` 尚未实现）
2. 历史汇总榜按「实例」还是按「程序（name|path）」聚合：现取实例口径（与实时页一致，但同名多实例各占一行）；若日后觉得噪音大，可改为按程序聚合，与用途键同粒度
3. SSE 无订阅者时降频到 5s 并暂停落盘聚合（原定随 M8 一并处理，M8 取消后此项独立保留）

---

## 15. 实现进度

| 里程碑 | 状态 | 交付物 |
|--------|------|--------|
| M1 采集骨架 | 已完成 | `src/collect/`、`src/app/collect_command` |
| M2 存储层 | 已完成 | `src/store/`、`src/app/record_command`、`src/app/db_command` |
| M3 HTTP/SSE/页面 | 已完成 | `src/api/`、`src/app/serve_command`、`frontend/src/views/ProcessesView.vue` |
| M4 服务线 | 已完成 | `src/collect/service_collector` + `src/api/service_store` + `serialize` + `server` 路由 + `serve_command` 接线 + `frontend/src/views/ServicesView.vue` |
| M5 规则库 | 已完成 | `src/analyze/`（usage_types / builtin_rules / rule_db）+ `store/db` 用途读写 + `/api/usage` 与 `/api/analyze` + `frontend/src/components/UsageCell.vue` |
| ~~M6~~ | 已取消 | 原计划的联网识别功能，经用户决定不做；项目定位为纯本地工具 |
| M7 · 趋势图/历史页 | 已完成 | `store/db` 三个区间查询 + `/api/stats` + `frontend/src/views/HistoryView.vue` + `frontend/src/components/LineChart.vue` |
| M7 · 进程树 | 已完成 | `collect/process_metrics` 增 ppid + `api/serialize` 输出 ppid + `frontend/src/views/ProcessesView.vue`（表格 ⇄ 进程树切换） |
| M7 · 启动项 | 已完成 | `collect/startup_collector`（注册表 Run ×3 + 启动文件夹 ×2 + IShellLink）+ `api/startup_store` + `/api/startup` + `frontend/src/views/StartupView.vue` |
| ~~M8~~ | 已取消 | 用户决定不做打包：工具为本地自用，编译后直接运行 `perfstat.exe` 即可；单文件 / 托盘 / 自启均不需要 |
| M9 · WebView2 壳 | 未开始 | 可选，用独立窗口 + WebView2 承载现有前端，免去「开浏览器 + 记端口」 |

第三方单文件库（`src/third_party/`）：

| 库 | 版本 | 文件 |
|----|------|------|
| cpp-httplib | 0.60.1 | httplib.h |
| nlohmann/json | 3.12.0 | json.hpp |
| SQLite | 3.54.0 | sqlite3.c / sqlite3.h / sqlite3ext.h |

构建要点：`project()` 启用 C 语言编译 sqlite3.c；`_WIN32_WINNT=0x0A00`（httplib 拒绝低于 Win10 的目标）。

M2 验收证据（2026-10-13 实测）：

```text
perfstat record --interval=1000 --duration=75   -> 落库 253 行，2 个分钟桶
perfstat db --stats                             -> dim_entity 127 / fact 253
perfstat db --purge --days=30                   -> 删除 0 行（数据在窗口内）
perfstat db --purge --days=0                    -> 删除 253 行（清理路径有效）
```

与设计的偏差（有意为之）：

1. **retention 并入 persist_worker**：清理同属写操作，必须与落库串行在同一线程，独立成模块反而割裂。第 11 节文件清单已同步。
2. **ring_buffer 延后到 M3**：环形缓冲只服务 SSE 实时推送，落库走「分钟累加器」即可。M2 不引入。
3. 新增 CLI 子命令 `record`（采集落库）与 `db`（概况 / 清理），作为 M2 验证入口；M8 托盘模式复用同一套 store 组件。

M3 验收证据（2026-10-13 实测）：

```text
perfstat serve --no-persist   -> 监听 127.0.0.1:17321，token 为 64 位 hex
/api/health 无 token          -> 401
/api/health?t=<token>         -> 200
/api/processes?t=<token>      -> JSON 数组
/api/realtime?t=<token>       -> SSE data 帧
vite dev 代理 /api            -> 转发 17321 成功
浏览器 /processes             -> 轮次递增；4.5s 内由 1 行变为 158 行，无控制台错误
```

M3 实现说明：

1. 新增子命令 `serve`：采样 + 落库 + HTTP/SSE 一体常驻，是 M8 托盘模式的内核。
2. SseHub 每订阅者一条独立队列（上限 8 帧，溢出丢最老）；无订阅者时跳过 JSON 序列化。
3. 前端 dev 期走 vite proxy（`/api` → `127.0.0.1:17321`），生产期由 exe 内嵌同源直连，前端代码用相对路径，不需区分。
4. 前端页面当前只做 `/processes` 表格（排序 / 过滤 / 后台开关），趋势图与进程树属 M7。
5. 「无订阅者降频到 5s」尚未实现，见第 14 节未决项 3（原打算随 M8 处理，M8 已取消）。

## M4 服务线（已完成）

新增 6 个文件 + 改 4 个：

```text
src/collect/service_collector.h/.cpp  SCM 枚举 + 三态占用关联
src/api/service_store.h/.cpp          ServiceSnapshot 线程安全存取
src/api/serialize.cpp                 新增 serialize_services
src/api/server.cpp/.h                 新增 /api/services 与 /api/services/{name}
src/app/serve_command.cpp             采样回调里 collect_round + service_store.update
frontend/src/views/ServicesView.vue   服务页（轮询 + 三态列 + 共享标注）
frontend/src/router/index.js          /services 路由
frontend/src/App.vue                  导航加「服务」
frontend/src/api/client.js            新增 getServices
```

关键实现点：

- SCM 元数据（名称/显示名/状态/启动类型/宿主 PID）来自 `EnumServicesStatusEx`，零提权依赖，每轮一次 RPC
- 启动类型经 `QueryServiceConfig` 取得，每 15 轮（约 30s）周期刷新缓存，避免每轮数百 RPC
- 占用按 D17 三态：`ok`（宿主 PID 在进程快照中 accessible）/ `denied`（被拒或缺失）/ `na`（未运行或无宿主）
- 共享宿主标注（D11）按 SCM 宿主 PID 聚合，**与占用可见性无关**——即使宿主被拒也显示「N 共享」
- 前端每 2s 轮询 `/api/services`，未走 SSE（服务变化慢，省开销）

M4 验收证据（2026-10-13 实测，非提权会话 `serve --no-persist --no-token`）：

```text
/api/services HTTP 200，共 291 个服务
三态分布：na 187 / denied 96 / ok 8
共享宿主服务 13 个，单宿主最多 5 个服务同驻（如 BFE 与同宿主服务共享 PID 4136）
/api/services/{name} 单服务详情 HTTP 200，返回对应条目
前端 npm run build：29 模块转换，0 错误，产物 100KB
```

与设计的偏差（有意为之）：

1. **延迟自动启动不单列**：`dwStartType` 对延迟自动服务仍为 `SERVICE_AUTO_START`，无独立枚举值（延迟是 `QueryServiceConfig2` 的单独标志），故映射为 `auto`。D17 仅要求 auto/manual/disabled，符合
2. **前端轮询而非 SSE**：服务变化频率远低于进程，2s 轮询 `/api/services` 足够，省去 SSE 帧开销；与 DESIGN 第 5 节「SSE 用于实时推送」不冲突（进程仍走 SSE）
3. **仅枚举 Win32 服务**：`SERVICE_WIN32` 覆盖 svchost 宿主与独立进程服务；驱动（kernel/file-system driver）未纳入，与任务管理器「服务」视图一致；如需驱动可后续加 `SERVICE_DRIVER`

**M4 已提交**：`d72b09c`（连同此前漏提交的 `CMakePresets.json`），其后 `a4ddf3c` 将 `build-debug/` 加入 `.gitignore`。当前提交历史：`086d27e`（M1）→ `c2aafac`（M2）→ `99d5d1f`（M3）→ `d72b09c`（M4）→ `a4ddf3c`。

## M5 规则库（已完成）

新增 5 个文件 + 改 9 个：

```text
src/analyze/usage_types.h             UsageEntry / EntityRef / AnalyzeResult + usage_key
src/analyze/builtin_rules.h/.cpp      内置规则表（进程 89 条 + 服务 61 条）
src/analyze/rule_db.h/.cpp            规则库：三级解析 + 人工覆盖 + 批量统计
src/store/store_types.h               新增 UsageRow + aggregate_instance_key
src/store/db.h/.cpp                   upsert_usage（带 manual 保护）+ load_usages
src/api/serialize.h/.cpp              进程/服务 JSON 附带 uk 与 usage
src/api/server.h/.cpp                 GET/POST /api/usage、POST /api/analyze
src/app/serve_command.cpp             接线规则库（挂库或纯内存）
frontend/src/api/client.js            apiPost + setUsage / getUsageOverrides / analyzeUsage
frontend/src/components/UsageCell.vue 用途单元格 + 编辑弹层
frontend/src/views/{Processes,Services}View.vue  用途列 + 「本地识别」按钮
```

关键实现点：

- **实体键取聚合口径**：用途挂在「进程名 + 路径」「服务名」这一层（D12 的聚合视图键），不随实例启动时间漂移。dim_entity 里聚合实体的 instance_key = `name|path`，与采集落库的 `name|path|start_100ns` 段数不同，不会撞键
- **manual 保护落在 SQL 里**：`ON CONFLICT(entity_id) DO UPDATE ... WHERE dim_usage.source <> 'manual' OR excluded.source = 'manual'`，日后任何自动回写来源都无法污染人工覆盖（D13）
- **锁的边界**：resolve 被采集线程（序列化）与 HTTP 线程（覆盖写）同时调用，故 RuleDb 内部持 mutex；写路径把 DB IO 放在锁外，避免阻塞每 2s 一轮的序列化
- **用途随帧附带**：序列化时就地解析并写进 JSON，前端零额外请求；覆盖保存后下一帧即生效，无需重启
- **serve 的两种存储态**：persist 时规则库挂一个独立 SQLite 连接（WAL 支持多连接，与写库线程互不阻塞）；`--no-persist` 时为纯内存，覆盖仅本会话生效

M5 验收证据（2026-10-13 实测）：

```text
内置命中（serve --no-persist --no-token）：
  进程约 258 条 → builtin 命中 177，未识别 82（未识别全是黑名单内的系统进程）
  服务 291 条   → builtin 命中 60
  命中样例：svchost.exe→系统服务 / dwm.exe→桌面外壳 / nvcontainer.exe→驱动与固件
人工覆盖：
  未识别项 GameInputRedistService.exe 覆盖后 source=manual，known 由 false 转 true
  覆盖 builtin 项 sihost.exe（桌面外壳）后 source 由 builtin 转 manual —— 优先级生效
持久化（serve --db=build/m5-test.db）：
  写入覆盖 → taskkill → 重启 → 启动日志「已有覆盖 1 条」→ GET /api/usage 精确匹配 1 条
  → 该进程 usage.source=manual，path 逐字符一致
批量识别 POST /api/analyze：total=549（258 进程 + 291 服务），
  builtin=237 / manual=2 / unknown=310（未识别项收进 pending 清单返回前端）
浏览器（vite 5173 + chromium 截图，0 控制台错误）：
  /processes 用途列 152 行，「浏览器 / Microsoft Edge 主进程」等正常呈现
  /services 用途列 291 行；点单元格弹编辑框（类别 / 用途 / 建议 + 来源提示）
```

与设计的偏差（有意为之）：

1. **`POST /api/usage/{id}` 改为 `POST /api/usage`**：实体定位放进 body 的 kind/name/path。进程路径含反斜杠与冒号，塞进 URL 既长又易被中间层改写；改后语义更完整（定位 + 内容一次给全）。另补 `GET /api/usage` 用于列出覆盖
2. **builtin 规则不落 dim_usage**：内置规则随 exe 发版更新，落库反而会与新版规则打架。dim_usage 只存 manual 这一层「与用户相关」的记录
3. **用途解析放在序列化阶段**：每轮约 300 次内存查表，成本可忽略，换来前端零额外请求与「保存即生效」

**M5 已提交**：`3853f2a`（18 文件 / 1645 插入）。当前提交历史：`086d27e`（M1）→ `c2aafac`（M2）→ `99d5d1f`（M3）→ `d72b09c`（M4）→ `a4ddf3c`（忽略 build-debug/）→ `3853f2a`（M5）。

## M7 历史页与趋势图（已完成）

新增 3 个文件 + 改 7 个：

```text
src/store/db.h/.cpp                   query_entity_stats / query_system_series / query_entity_series
src/api/server.h/.cpp                 GET /api/stats（区间聚合 + 曲线 + 单实体序列）
src/api/client.js                     getStats({from,to,bucket,top,entityId})
frontend/src/components/LineChart.vue 手写 SVG 折线图（零图表库依赖）
frontend/src/views/HistoryView.vue    时间范围预设 + 三条曲线 + 汇总榜表格
frontend/src/router/index.js          /history 路由
frontend/src/App.vue                  导航加「历史」
```

关键实现点：

- **不引图表库**：曲线用手写 SVG（`viewBox` 0 0 900 110，`preserveAspectRatio="none"` 横向拉伸）。前端维持「零额外依赖」的既有取向，产物体积不因 M7 上涨
- **降采样（落实第 14 节原未决项）**：点数超过可用像素宽度时按 `step = n / limit` 等距抽稀，末尾强制保留最后一点，避免长区间撑出上万个 `<path>` 顶点
- **三个查询各司其职**：`query_entity_stats` 按 `entity_id` GROUP BY（`ORDER BY AVG(cpu_avg) DESC LIMIT ?`）；`query_system_series` 用 `(bucket_ts / ?) * ?` 把分钟桶归并成更粗的桶再求和；`query_entity_series` 只取单实体
- **桶粒度随区间走**：前端预设 1 小时/60s、6 小时/300s、24 小时/900s、7 天/3600s，区间越长桶越粗，请求与渲染成本恒定
- **辅助连接复用**：`serve --db` 下规则库与历史查询共用一条辅助 SQLite 连接（WAL 支持多连接，写库线程不被阻塞）；`--no-persist` 时 `/api/stats` 直接返回 503
- **历史汇总取实例口径**：与 `/processes` 实时页一致，同名多实例各占一行（见第 14 节未决项 2）

M7 验收证据（2026-10-13 实测）：

```text
造数：perfstat record --interval=1000 --duration=150   -> 落库 382 行 / 3 个分钟桶
serve --db=build/m7-test.db                            -> 启动打印「历史数据：事实行 382 | 区间 …」
GET /api/stats?from&to&bucket=60&top=10
  -> 汇总 10 行 / 曲线 4 点，与 SQLite 直查逐行一致（m7_verify.py：结论：全部一致）
GET /api/stats?…&entity_id=<id>                        -> 单实体序列点数 == 该实体 buckets
浏览器 /history（vite 5173 + chromium 截图，0 控制台错误）：
  汇总榜 50 行（类型/名称/用途/CPU 均值/峰值/内存均值/峰值/读/写/分钟）
  默认两条曲线「CPU 合计」「内存合计」；点某行（分钟>1）另起该实体 CPU 曲线，图数 2 -> 3
```

与设计的偏差（有意为之）：

1. **`/api/stats` 参数扩展**：契约原写 `?from&to`，实现补上 `bucket`（桶粒度）、`top`（汇总行上限）、`entity_id`（单实体序列），一次接口覆盖三条曲线与汇总榜
2. **单实体曲线点数为 1 时不绘制**：区间短或实例存活不足 2 个桶时只显示「该区间无数据」，不画退化线段

## M7 进程树（已完成）

改 3 个文件：

```text
src/collect/process_metrics.h   ProcessSample 增 ppid 字段
src/collect/process_collector.cpp  从 PROCESSENTRY32W.th32ParentProcessID 填 ppid
src/api/serialize.cpp           进程 JSON 增 "ppid"
frontend/src/views/ProcessesView.vue  表格 ⇄ 进程树切换 + 缩进/折叠
```

关键实现点：

- **不加系统调用**：父进程 PID 与枚举走的是同一个 `CreateToolhelp32Snapshot`，`PROCESSENTRY32W` 本就带 `th32ParentProcessID`，等于零成本
- **父进程不在当前集合内即为根**：进程树在**过滤后**的集合上构建（受「仅后台」与关键字过滤影响），父进程被过滤掉的节点升为根，树始终自洽、不留悬空节点
- **折叠用「已访问但不出行」表达**：被折叠的子树仍走完遍历并标记 visited，避免被「环兜底」逻辑当成根重复挂出
- **环与深链兜底**：父子互指（或长链）由 `visited` 集合 + 深度上限 64 兜住，不会递归爆栈；成环导致没有根可达的节点，逐个补挂成根
- **缩进上限 12 层**：每层 16px，超过 12 层不再加深，避免窄屏下名称被挤出可视区
- **折叠三角用 CSS 画**：不用 `▾/▸` 字符，规避字体缺失时回退成豆腐块

M7 进程树验收证据（2026-10-13 实测）：

```text
Debug / Release 均编译通过、零警告
浏览器 /processes（vite 5173 + chromium，0 控制台错误）：
  表格 134 行 ⇄ 进程树 134 行（同一帧一致；来回切 4 次读数全为 134/134）
  树里 134 行 = 134 个不同 PID（无重复，未因构树丢行）
  有子节点（带三角）39 行；缩进行 78，最大缩进 92px（5 层）
  折叠一行 134 -> 114；「全部折叠」-> 56 根；「全部展开」-> 134（精确还原）
同源交叉验证（build/tree_check.py 直取 /api/processes 在 Python 里复现构树）：
  快照 244 条 -> 过滤后 121 条 -> 树可达 121 条（未丢行）；根 56、有子节点 33、最大层级 8
```

**进程树已提交**：`0075030`（5 文件 / 251 插入 / 14 删除）。

## M7 启动项（已完成）

新增 4 个文件 + 改 7 个：

```text
src/collect/startup_collector.h/.cpp   注册表 Run ×3 + 启动文件夹 ×2 + IShellLink 解析
src/api/startup_store.h/.cpp           启动项清单的线程安全存取（与 service_store 对称）
src/api/serialize.cpp                  新增 serialize_startup（占用按当轮进程快照现算）
src/api/server.h/.cpp                  构造增 startup_store + GET /api/startup
src/app/serve_command.cpp              首采 + 每 15 轮刷新，启动横幅打印项数
CMakeLists.txt                         补链接 ole32 / uuid（COM 解析 .lnk）
frontend/src/api/client.js             getStartup
frontend/src/views/StartupView.vue     列表：名称/命令/来源/范围/状态/占用/PID，可排序
frontend/src/router/index.js + App.vue  /startup 路由与「启动项」导航
```

关键实现点：

- **五处来源**：HKCU Run、HKLM Run（64 位视图）、HKLM Run（32 位视图）、用户启动文件夹、公共启动文件夹；注册表用 `KEY_WOW64_*` 显式区分视图，而不是去拼 `WOW6432Node` 路径
- **启用状态的分桶**：`StartupApproved` 会残留「对应 Run 值早已删除」的历史记录，所以禁用名单必须按「作用域标签 + 名」分桶。最初揉成一个集合按名查找，会拿 HKCU 的残留去误伤 HKLM 的同名条目——这是实现中逼出来的修正
- **命令 → exe 路径**：`REG_EXPAND_SZ` 先展开 `%ENV%`；带引号取引号内，未加引号则在 `.exe` 结尾处截断（容忍路径含空格）；`.lnk` 走 `IShellLink` 取目标与参数
- **`desktop.ini` 必须跳过**：启动文件夹里的元数据文件带 `SYSTEM` 属性，不是自启项，任务管理器也不显示它
- **占用三态（沿 D17）**：`ok` 出值 / `denied` 进程在跑但句柄被拒（多为提权进程，前端标「无权限」）/ `na` 没在跑——避免把「读不到」渲染成 `0 KB` 被误读为「不占内存」
- **清单缓存 + 占用现算**：清单一改不必频繁重读注册表；序列化时用当轮进程快照匹配（先按 exe 全路径，不中再退化为按文件名），占用始终是最新一轮的值
- **额外信号**：「已禁用，却在跑」单独标成琥珀色——说明它虽被取消自启，但当下仍以别的方式在运行

M7 启动项验收证据（2026-10-13 实测）：

```text
Debug / Release 均编译通过、零警告（新增链接 ole32 / uuid 正常）
启动横幅：启动项 13 项（每约 30s 重读）
GET /api/startup：13 项，来源分布 HKCU Run 8 / HKLM Run（64 位）4 / 启动文件夹（用户）1
  启用 2 / 禁用 11；exe 路径解析率 13/13；desktop.ini 已被过滤（0 残留）
  占用三态：ok 2 / denied 1（Everything 为提权进程）/ na 10
编码规则交叉核对：StartupApproved 首字节「偶数=启用、奇数=禁用」
  与 DFIR 分析文章、微软社区答复、Nutanix 文档三处独立来源一致
浏览器 /startup（vite 5173 + chromium，0 控制台错误）：
  13 行；默认按 CPU 降序；「仅看启用」后 2 行（等于启用数）；
  点「名称」表头切换为按名降序（Steam / StartAUEP / Snipaste / SecurityHealth）
```

**启动项已提交**：`cbff9c1`（15 文件 / 1134 插入 / 13 删除）。至此 M7「趋势图/历史页 + 进程树 + 启动项」三项全部完成。当前提交历史：`086d27e`（M1）→ `c2aafac`（M2）→ `99d5d1f`（M3）→ `d72b09c`（M4）→ `a4ddf3c`（忽略 build-debug/）→ `3853f2a`（M5）→ `d4329b4`（M7 历史页 + M6 取消）→ `0075030`（M7 进程树）→ `cbff9c1`（M7 启动项）。



