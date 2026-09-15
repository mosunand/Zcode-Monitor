# zcode-monitor

<img src="resources/logo-256.png" width="72" align="right" alt="logo">

zcode-monitor 的 **Qt Widgets 原生桌面重写版** —— 用 C++/Qt 6 重写的 ZCode agent 本地观测面板，功能与原 Node.js/Web 版完全对等（只多不少）。

只读访问 `~/.zcode/cli/` 下的 SQLite 主库与 JSONL 事件流，把一次 agent 运行中发生的一切（模型请求、token 消耗、工具调用、子 agent 派生、推理链）整理成可查、可追溯的桌面界面。**全程不修改任何 ZCode 数据**（唯一例外：ZCode 退出后自动/手动执行一次 WAL checkpoint，把内存中的数据折入主库以保住历史）。

## 功能总览（与 Web 版对等）

| 视图 | 功能 |
|---|---|
| **实时监控** | 9 张 KPI 卡（模型调用/时延/输入/输出/**总 token**/推理占比/工具/活跃会话/错误率）+ 平均 token 速度卡 + 3 张图表（每小时调用柱状、token 构成多系列折线、逐点着色 tok/s 折线，全部 QPainter 自绘，悬停有 tooltip）+ 最近请求速度表（加权均速表尾）+ 实时活动流（1.5s 轮询推送，新行闪烁动画，保留 60 条）+ 按模型/按工具算力分布表；时间窗：今天 / 24h / 7d |
| **会话** | 左列表（搜索 / 类型过滤 / 三种排序，500 条）+ 右侧 7 个标签页：**Timeline**（子 agent 事件流：metadata 头卡 + 7 类过滤 + 连续流式增量折叠 + payload 展开）、**Context**（完整对话重放：turn 分组侧栏 + 消息流 + reasoning 默认折叠 + 工具调用懒加载 stdout/stderr + 双向滚动联动 + 展开/收拢全部）、**Turns**（时长条 + 状态着色）、**Agents**（子 agent 表 + 深链）、**Tasks**（todo 徽章列表）、**Usage**（11 列 token 明细）、**State**（16 项元数据） |
| **子 Agent** | parent_id 级联关系树，折叠/展开，双击或按钮直达会话 |
| **错误与链路** | 三张错误汇总卡 + 模型/工具失败表（行点击→trace 还原）+ 最慢工具 Top 30 + trace_id 链路瀑布图（自绘，含原始 span 树 JSON） |
| **原始数据** | 19 张表白名单浏览器，排序/降序/where 条件，JSON 单元格美化展开，行 JSON 查看 |
| **运行原理** | ER 图 + turn 流程图 + 8 张概念卡（全部用你的真实数据插值）+ 真实推理片段示例 |
| **全局** | Dark/Light 双主题（按钮 / `t` 快捷键 / QSettings 持久化）、顶栏健康轮询（DB 状态 · ZCode 运行中 · WAL 大小）、WAL checkpoint 按钮 + toast |

## 相对 Web 版的增强

- **Windows 进程探测修复**：原版 `ps -axo comm` 在 Windows 恒失败 → 自动 checkpoint 从不触发；本版用原生 Toolhelp32Snapshot 枚举进程，ZCode 退出后真正自动折叠 WAL。
- **实时流水位修复**：原版 SSE 水位初始化取了全表最老一行（开机回放全部历史）；本版取最新行，只推应用启动后的新事件。
- **span 树构建修复**：原版 `indexOf` 是 O(n²) 且重复挂载子节点导致深链爆炸；本版改为索引 Map + 防环。
- **原始数据查看器**：where 输入保留（与原版一致的调试能力），但表名/排序列有白名单校验。
- **图表全部自绘**：无 Chart.js/CDN 依赖，离线可用。
- **`--selftest`**：对真实数据库跑 21 项数据层检查，输出 JSON 化的验证报告（exit code 0/1）。
- **`--screenshot [dir]`**：自动遍历全部页面/标签页截图保存后退出，用于回归验证。

## 环境要求

- Qt 6.8+（Widgets + Sql 模块；mingw 或 msvc 均可）
- CMake ≥ 3.21 + Ninja（或其它生成器）
- 编译器支持 C++17
- 已安装并至少运行过一次 ZCode 客户端（产生 `~/.zcode/cli/` 数据）

构建环境完全来自环境变量（PATH / CMAKE_PREFIX_PATH），**工程内不硬编码任何路径、不引入 vcpkg 等第三方包管理**。

## 构建

```bash
cmake -B build -G Ninja
cmake --build build
```

产物：`build/bin/zcode-monitor.exe`

## 运行

```bash
./build/bin/zcode-monitor.exe              # 打开主界面
./build/bin/zcode-monitor.exe --selftest   # 无头数据层自检
./build/bin/zcode-monitor.exe --screenshot shots  # 自动截图导览（回归验证用）
```

### 环境变量（与原版同名同义）

| 变量 | 默认 | 说明 |
|---|---|---|
| `ZCODE_DB` | `~/.zcode/cli/db/db.sqlite` | SQLite 主库路径 |
| `ZCODE_LOG_DIR` | `~/.zcode/cli/log` | 每日 JSONL 日志目录 |

## 数据源（全部只读）

| 来源 | 路径 |
|---|---|
| SQLite 主库 | `~/.zcode/cli/db/db.sqlite`（QSQLITE 只读 + busy_timeout=5000 + 四层重试/自愈） |
| 子 agent 事件流 | `~/.zcode/cli/agents/<parent>/agent_*/transcript.jsonl` + `metadata.json` |
| 每日日志 | `~/.zcode/cli/log/zcode-YYYY-MM-DD.jsonl`（UTC 日，尾部 1MB 高效读取） |
| Bash 输出 | `~/.zcode/cli/exec/<sess>/<callId>-stdout.log`（200KB/64KB 截断） |

## 架构

```
src/
├── core/      数据层（无 Qt Widgets 依赖）
│   ├── DbService         SQLite 只读访问 + 全部查询（db.js 移植）
│   ├── RuntimeWatchdog   进程探测 + WAL checkpoint 看门狗（zcode-runtime.js 移植+Windows修复）
│   ├── LivePoller        1.5s 轮询 → 信号直推（SSE 的进程内等价物）
│   ├── TranscriptService transcript.jsonl 解析/分类/摘要/聚合（transcript.js 移植）
│   ├── LogTailService    日志尾读 + trace 事件收集 + span 森林（log-tail.js 移植）
│   ├── RawService        白名单原始表查询（routes/raw.js 移植）
│   ├── Paths / Types     路径解析 / 共享数据结构
├── ui/        界面层
│   ├── MainWindow        顶栏 + QStackedWidget 导航 + 健康轮询 + toast
│   ├── Theme             双主题调色板（styles.css 全量移植）+ 全局 QSS
│   ├── UiUtil            徽章/圆点/卡片/节标题等基元
│   ├── widgets/          KpiCard · KpiGrid · MiniChart（自绘图表）· LiveFeed（实时流）
│   └── pages/            Overview · Sessions(+7 Tab) · TimelineTab · ContextTab · Agents · Errors(+瀑布) · Raw · How
├── util/      Format（app.js 格式化函数全量移植）
└── selftest/  --selftest 无头验证
```

## 与原版对照验证

数据层已与原 Node 版并排对比（同一数据库）：

- KPI（calls/completed/errors/tokens/cache/活跃会话）、时序、模型/工具分布、速度统计：**逐字段一致**
- 会话列表 / agents 森林（91 会话 48 根）：**一致**
- transcript 定位/解析/聚合、日志尾读、trace 森林：**一致**
- GUI 视觉验证：`--screenshot` 全页面 14 张截图像素校验通过

## 质量保证

- **3 轮独立代码审查**（对照原版逐行核对 SQL/算法/生命周期）：共发现并修复 P0×1、P1×5、P2×19、P3×57；关键修复包括 WAL checkpoint 连接重构遗漏（P0）、只读打开未落实、实时流批内顺序、主题切换状态保留（ContextTab 展开状态 / TimelineTab 过滤器 / 列表选中态全部随主题往返）、表格 setSpan 残留、自绘控件垫色
- **回归基线**：编译零错误零警告（含 -Wdeprecated 清理）；`--selftest` 21 项断言全过；14 张截图像素校验（深浅主题、状态点、徽章、图表着色）全过
- **规模**：48 个源文件，约 10,100 行 C++（core 2,854 / ui 6,766 / util 140 / selftest 253）

## 界面细节

- 双主题（Dark/Light）完整调色板：40+ 设计 token × 2，QSS 动态属性系统保证所有控件颜色随主题即时切换
- 图表 QPainter 自绘：nice-number 坐标轴、基线强调、图例、悬停十字线 + 圆角阴影 tooltip、柱状圆角、多系列渐变填充、逐点着色
- 徽章/速度 chip 胶囊渲染（30%/10% 派生底色 + 35% 边框）、状态点柔光、KPI 卡缓存命中率进度条、会话列表选中态左侧强调条
- 实时活动流：新行 1.2s 闪烁渐隐、error 行底色、60 条滚动上限、空态占位

## 许可

MIT（与上游 zcode-monitor 一致）
