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
| 4 | 自动分析判断每个服务的用途 | analyze/rule_db + llm_client |
| 5 | 基于占用数据给出针对性清理建议 | analyze/rule_db + /api/usage |
| 6 | 预留扩展功能 | 趋势图 / 进程树 / 启动项管理 |

**明确不做**：多机上报汇总、网络 IO 按进程统计（需 ETW）、跨平台。

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
| D13 | 规则库 | builtin 内置 + llm 自动回写 + manual 人工覆盖，三者共存不互相污染 |
| D14 | 实时推送 | SSE，非轮询非 WebSocket |
| D15 | LLM 落位 | 主路径直接调 DeepSeek Chat API；仅「允许深查」时起 DeepSeek Harness |
| D16 | 分发 | 单文件 exe；数据落 `%LOCALAPPDATA%\PerfStat\` |
| D17 | 轻量化 | 内存 ≤ 40MB，CPU 均值 ≤ 0.3% / 峰值 ≤ 2%；无前端连接时自动降频 |

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
│  analyze/    规则库、LLM 客户端、熔断器、价格闸门、深查     │
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

外部依赖（可选、可整体关闭）：
  DeepSeek Chat API  ── 主路径，单轮无状态请求
  dsh（DeepSeek Harness）── 仅「允许深查」时按需 spawn
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

**连接数即采样节拍**：有 SSE 订阅者时采样间隔 2s；无订阅者时降到 5s 且暂停落盘聚合。这是 D17 轻量化的主要手段。

**鉴权**：绑定 `127.0.0.1`；启动时生成 32 字节随机 token 写入进程环境，打开浏览器时带 `?t=<token>`，前端存入 sessionStorage，所有 `/api/*` 请求带 `X-Token`。防止本机其他进程滥用 `/api/actions/*`。

**端口**：默认 17321，被占用则自动 +1，并把最终端口写入启动 URL。

---

## 6. 数据采集与分析方法

### 6.1 进程采集（2s 一轮）

1. `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` 枚举全部 PID
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

`OpenSCManager` + `EnumServicesStatusEx` 枚举全部服务，取服务名、显示名、状态、启动类型、宿主 PID。再把宿主 PID 关联到进程采集结果，得到 D11 的「N 个服务共享」。

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

三级瀑布，逐级降级：

```
1. 查本地库 dim_usage
   命中 → 直接返回（零成本）

2. 未命中 → 打包批量问 DeepSeek（走价格闸门 + 熔断器）
   返回 JSON 数组：{name, category, purpose, suggestion, confidence}
   → 回写本地库，source = 'llm'

3. 调用被拒绝 / 失败 / 熔断 → 返回 { category: '其他', purpose: '未知' }
   主功能不受影响
```

人工在界面上改过的条目 `source = 'manual'`，后续 LLM 回写**不得覆盖** manual 记录。

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
    source     TEXT    NOT NULL,     -- 'builtin' | 'llm' | 'manual'
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
%LOCALAPPDATA%\PerfStat\holidays.json
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
| GET | `/api/stats?from&to` | 时间段聚合查询 |
| GET | `/api/startup` | 启动项（Run 项 + 启动文件夹） |
| POST | `/api/analyze` | 批量识别（走价格闸门 + 熔断器） |
| POST | `/api/usage/{id}` | 人工覆盖标签（source = manual） |
| POST | `/api/actions/terminate` | 结束进程（内部 spawn 提权 helper） |
| POST | `/api/actions/service` | 停 / 启服务（同上） |
| GET | `/api/llm/status` | 熔断状态 + 今日用量 + 当前是否高峰 |
| GET | `/api/settings` | 读配置（**不回显 key**） |
| POST | `/api/settings` | 写配置 |

---

## 9. LLM 层：价格闸门 + 熔断 + 深查

### 9.1 峰谷价格闸门

DeepSeek 采用峰谷定价，**空闲时段价格 = 高峰时段的一半**。

| 时段 | 范围（北京时间） |
|------|-----------------|
| 高峰 | 周一至周五（非法定节假日）9:00–12:00、14:00–18:00 |
| 空闲 | 其余全部，含夜间、周六周日、法定节假日 |

`deepseek-flash`（V4.1-Flash）价格，元 / 百万 tokens：

| 项 | 空闲 | 高峰 |
|----|------|------|
| 输入（缓存命中） | 0.02 | 0.04 |
| 输入（缓存未命中） | 1 | 2 |
| 输出 | 4 | 8 |

**单次成本估算**：一次批量 20 条实体，输入约 1000 tokens、输出约 800 tokens → 高峰 ≈ 0.84 分，空闲 ≈ 0.42 分。300 次/天全高峰 ≈ 2.5 元/天，全空闲 ≈ 1.26 元/天，月最坏约 75 元。

**闸门行为**：处于高峰时段时，前端弹确认框——

> 当前为 DeepSeek 高峰时段，价格为空闲时段的 2 倍（约 0.84 分/次）。建议等到 12:00 后或 18:00 后再识别。

三个按钮：**现在调用** / **稍后** / **本时段不再问**。

设置里额外给一个更省钱的选项：**高峰时段一律不调用，攒到空闲时段批量跑**（默认关闭）。

**节假日维护**：法定节假日需要人工维护，用一个 `holidays.json` 存「年份 → 日期数组」，每年更新一次；设置里给「今天强制按空闲处理 / 强制按高峰处理」的手动开关兜底。判断逻辑若取不到当年数据，退化为「只按工作日+时段判断」。

### 9.2 熔断机制（六层）

| 层 | 内容 |
|----|------|
| L1 收敛触发 | 只对本地库未命中的新实体发请求；命中即返。每轮打包 ≤ 20 条/次 |
| L2 结构免疫 | 主路径单轮、无状态、不带工具、不带历史——无 tool-use 即无循环载体 |
| L3 上游限死 | `max_tokens=1024`、`n=1`、非流式；单请求超时 15s，超时即弃并记 unknown，**不重试** |
| L4 预算闸门 | 三级配额 + 连续失败熔断（见下表） |
| L5 兜底降级 | 拒绝或失败一律返回「未知/其他」，主功能不受影响 |
| L6 可观测 | 前端计数器（今日 X/Z 次、token 估算）+ 审计日志 + 一键总开关；key 单独申请，可单独吊销 |

| 参数 | 值 |
|------|-----|
| 单次批量上限 | 20 条实体 |
| 每分钟 | 6 次 |
| 每小时 | 60 次 |
| 每日 | 300 次 |
| 连续失败熔断 | 3 次 → 停 30 分钟，半开放 1 次试探 |
| 单请求超时 | 15s，不重试 |
| 单次深查 | 8 轮 / 180s 硬超时，到点 kill 子进程 |

**结论**：主路径物理上不可能死循环；唯一有循环风险的是 harness 深查路径，因此该路径必须带轮次与超时上限。

### 9.3 深查（DeepSeek Harness）

- 仓库：`github.com/deepseek-ai/deepseek-harness`，MIT，TypeScript monorepo，开发者预览版，官方声明会有破坏性变更
- 它是**agent harness**（类似 Claude Code 的运行时），不是推理 API；自身读 `DEEPSEEK_API_KEY` 调 DeepSeek 接口
- 依赖 Node `^22.19 || >=24`（本机 22.22.2 满足）
- 接入方式：以 headless CLI 子进程形式按需 spawn，跑完回收；总开关关闭时完全不启动
- **适用范围严格限定**：仅在用户对单条实体点「允许深查」时使用。让它去读 exe 版本资源、查注册表发布者、必要时联网搜证，产出用途判断后回写本地库

### 9.4 隐私边界

- 默认外发内容仅：进程名、可执行文件名、发布者、服务显示名与描述
- **默认不发完整命令行**；单条实体手动点「允许深查」才发更多
- 总开关关闭后，全程只走本地规则库，不产生任何网络请求

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
│  │                               # sampler / ring_buffer
│  ├─ store/                       # db / aggregator / persist_worker
│  ├─ analyze/                     # rule_db / llm_client / circuit_breaker
│  │                               # price_gate / deep_dive
│  ├─ api/                         # server / routes_* / sse_hub
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
| `/settings` | 设置：LLM key、总开关、采样参数、熔断用量、日志 |

---

## 13. 里程碑

| # | 内容 | 验收标准 |
|---|------|---------|
| M1 | 采集骨架 | 控制台打印进程 CPU/内存，与任务管理器数值一致 |
| M2 | 存储层 | 1 分钟聚合落库，30 天清理生效 |
| M3 | HTTP + SSE + 最小页面 | 浏览器 `/processes` 实时刷新 |
| M4 | 服务线 | 服务列表 + 共享宿主标注 |
| M5 | 规则库 | builtin 命中 + 人工覆盖生效 |
| M6 | LLM 层 | 价格闸门弹出、熔断计数正确、深查可用 |
| M7 | 扩展项 | 趋势图 / 进程树 / 启动项三个页面完成 |
| M8 | 打包 | 单文件 exe + 托盘 + 自启，内存 CPU 达标 |
| M9 | WebView2 壳 | 可选，不阻塞前八项 |

---

## 14. 未决项（编码中再定）

1. 提权 helper 的退出码约定与错误分类
2. 趋势曲线的降采样算法（按视口宽度抽稀）
3. dsh headless CLI 的具体调用参数（需读其文档确认）
4. 单文件 exe 的内嵌资源体积上限（Vue 产物约 100–300KB，预计无压力）

---

## 15. 实现进度

| 里程碑 | 状态 | 交付物 |
|--------|------|--------|
| M1 采集骨架 | 已完成 | `src/collect/`、`src/app/collect_command` |
| M2 存储层 | 已完成 | `src/store/`、`src/app/record_command`、`src/app/db_command` |
| M3 及以后 | 未开始 | — |

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
