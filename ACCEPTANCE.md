# 功能对等验收清单（Qt 版 vs Web 版）

对照 E:\Applications\zcode-monitor-main（Node/Express/原生 JS 版）逐项核对。
✅ = 已实现且验证；➕ = Qt 版增强；❌ = 缺失

## 全局 shell（app.js / index.html）

| # | Web 版功能 | Qt 版 | 状态 |
|---|---|---|---|
| 1 | 顶栏状态点（绿=DB 可读 / 红=不可读） | Dot 控件，healthTick 5s 轮询 | ✅ |
| 2 | 顶栏 meta：DB OK · ZCode 运行中 · WAL x.xMB 待合并 | 同文案同规则（walBytes>0 才显示） | ✅ |
| 3 | 6 项导航（实时监控/会话/子Agent/错误与链路/原始数据/运行原理） | QToolButton + QStackedWidget | ✅ |
| 4 | checkpoint 按钮（force 语义 + toast 提示合并量） | 同实现，含 zcode_running 409 语义 | ✅ |
| 5 | 主题切换按钮 🌙/☀️ | 同 | ✅ |
| 6 | 快捷键 t 切主题（输入框聚焦时不触发） | QShortcut + focus 控件判断 | ✅ |
| 7 | 主题持久化（localStorage） | QSettings | ✅ |
| 8 | toast 2400ms 自动消失 | QTimer 2400ms | ✅ |
| 9 | hash 路由深链 #/sessions/{id}/{tab} | openSession(id, tab) 导航接口 | ✅ |
| 10 | 503 指数退避重试（300/600/1200ms） | DbService 四层重试（更彻底） | ➕ |
| 11 | ?theme= URL 参数 | 不适用（桌面无 URL） | — |

## 实时监控（overview.js）

| # | 功能 | 状态 |
|---|---|---|
| 1 | 时间窗 today/24h/7d 下拉 + 刷新按钮 | ✅ |
| 2 | 9 张 KPI 卡（8 张与 web 同文案 + **总 TOKEN** 卡为 Qt 版新增：输入+输出+推理求和，进度条显示生成占比） | ✅ ➕ |
| 3 | 输入 token 卡：缓存命中百分比条 | ✅ |
| 4 | 推理占比着色（>30% 紫 / >5% 青 / 默认蓝） | ✅ |
| 5 | 错误率 >5% 红色 | ✅ |
| 6 | 平均 Token 速度卡（加权 t/s，tier 着色） | ✅ |
| 7 | 柱状图：模型调用/小时（悬停 tooltip） | ✅ QPainter 自绘 |
| 8 | 多系列折线：token 构成（输入/输出/推理，填充） | ✅ |
| 9 | 逐点着色折线：速度随时间 | ✅ |
| 10 | 最近请求速度表（7 列 + t/s chip 着色 + source 徽章） | ✅ |
| 11 | 表尾汇总（均速/总token/请求数/subagent 数，前端重算） | ✅ |
| 12 | 实时活动流（新行 1.2s 闪烁、error 底色、保留 60 条、空态文案） | ✅ |
| 13 | 按模型/请求来源表（provider 副行、来源徽章着色） | ✅ |
| 14 | 按工具表（错误红/零灰、均时延/最大/输出字节） | ✅ |
| 15 | SSE 推送 | LivePoller 1.5s 轮询+信号直推（进程内等价，修掉水位 bug） | ➕ |
| 16 | 图表主题适配（切主题重绘） | ✅ Theme::changed → renderData |

## 会话（sessions.js）

| # | 功能 | 状态 |
|---|---|---|
| 1 | 左列表：搜索（id/标题/目录）+ 类型下拉 + 排序（最近/token/调用） | ✅ 纯前端过滤排序同 web |
| 2 | 列表项：标题 + 相对时间 + main/subagent 徽章 + tok + req | ✅ |
| 3 | 详情头：标题 + subagent 徽章 + id + parent 短 id + 目录 | ✅ |
| 4 | 空态"← 从左侧选择一个会话" | ✅ QStackedLayout 占位 |
| 5 | Timeline tab：见下节 | ✅ |
| 6 | Context tab：见下节 | ✅ |
| 7 | Turns tab：时长条（error红/cancelled黄/默认蓝，宽度=duration/max）+ turn 短 id + ⚠context_exceeded + 统计 | ✅ |
| 8 | Agents tab：profile 徽章（Explore teal / 其他 purple）+ prompt 前 80 字 + token + 创建时间 + 打开深链 | ✅ |
| 9 | Tasks tab：status 徽章（pending黄/in_progress蓝/completed绿）+ priority 徽章（high红/medium黄/low灰）+ 内容 | ✅ |
| 10 | Usage tab：11 列（tool err 红、reasoning 紫） | ✅ |
| 11 | State tab：16 项 key-value | ✅ |
| 12 | 列表 500 条上限 | ✅ |

## Context tab（sessions.js renderContext）

| # | 功能 | 状态 |
|---|---|---|
| 1 | turn 分组（孤儿消息折叠进下一个真实 turn；结尾孤儿进最后一个） | ✅ 算法逐行对照 |
| 2 | rail 节点：状态色点 + 摘要（cleanText 剥 XML→user 首文本→assistant 首文本→Turn N）+ ⚙×N·tok·时长 | ✅ |
| 3 | 摘要 40 字截断 + tooltip 全文 | ✅ |
| 4 | 展开全部/收拢全部按钮 | ✅ |
| 5 | 消息卡：#序号 + role 着色（user 蓝/assistant 紫）+ meta（model/variant/mode/agent/turn/tokens）+ 时间 | ✅ |
| 6 | part 渲染：text / reasoning（紫侧边块默认折叠）/ tool（⚙+状态徽章+懒加载 output）/ step-finish / timeline / compaction / file（图片内嵌） | ✅ |
| 7 | 工具 output 200KB/64KB 截断 | ✅ |
| 8 | rail 点击 → 平滑滚动到该 turn 首条消息 | ✅ QPropertyAnimation 200ms |
| 9 | 滚动反向高亮当前 turn | ✅ 顶 40% 带重叠比例（IntersectionObserver 语义） |

## Timeline tab（timeline.js）

| # | 功能 | 状态 |
|---|---|---|
| 1 | 无 transcript 时引导卡（文案 + 去 Context 深链） | ✅ |
| 2 | 头卡：profile 徽章 + 描述 + kv（状态/耗时/总token/工具调用/事件总数/parent/spawn by）+ Top8 工具徽章 | ✅ |
| 3 | 7 个过滤按钮（全部/prompt/llm→/tools/network/usage/⚠错误）+ 显示 N/M | ✅ |
| 4 | coalesceStreaming（text_delta→"文本输出+N字符"、reasoning_delta→"◆ think 推理输出+N字符"） | ✅ |
| 5 | isError（resultType≠success 或 status/resultType 含 error/fail） | ✅ |
| 6 | 事件行：seq/时间/类别点/label+summary/payload▾ | ✅ delegate 绘制 |
| 7 | 点击行展开 payload JSON（缩进美化） | ✅ |
| 8 | 4000 条 limit | ✅ |

## 子 Agent 页（agents.js）

| # | 功能 | 状态 |
|---|---|---|
| 1 | "N 个会话 · M 个根会话"摘要 | ✅ |
| 2 | 树：● 主（蓝）/ └ 子（青）缩进 + stats + 相对时间 + 打开深链 | ✅ |
| 3 | 折叠/展开 | ✅ 双击行或按钮 |

## 错误与链路（errors.js）

| # | 功能 | 状态 |
|---|---|---|
| 1 | window 下拉（today/24h/7d/all）+ 刷新 | ✅ |
| 2 | 三张汇总卡（按模型错误类型/按工具/按工具错误类型） | ✅ |
| 3 | 模型失败表（7 列，错误 120 字截断红字） | ✅ |
| 4 | 工具失败表（7 列，exit 码） | ✅ |
| 5 | 行点击 → trace_id 带入还原 | ✅ |
| 6 | 最慢工具 Top30（错误 80 字截断） | ✅ |
| 7 | trace 还原：输入框 + Enter/按钮触发 | ✅ |
| 8 | 瀑布图（时间 8 字符、事件+module、left≤60%、width 2-40px、tool青/model紫/其他灰、时长列） | ✅ 自绘 |
| 9 | 无事件提示（UTC 日滚动文案） | ✅ |
| 10 | 原始 span 树 JSON 折叠查看 | ✅ |
| 11 | 会话列点击 → 打开会话 timeline | ✅ |

## 原始数据（raw.js）

| # | 功能 | 状态 |
|---|---|---|
| 1 | 表下拉（web 14 张 + 后端白名单 19 张全集） | ➕ |
| 2 | 排序列下拉 + 降序勾选 + where 输入 + Enter 查询 | ✅ |
| 3 | 行数提示 + JSON 列 {…} 显示 + 双击展开美化 | ✅ |
| 4 | 行 JSON 查看 | ✅ |
| 5 | 100 行 / 后端 1000 上限 | ✅ |

## 运行原理（how.js）

| # | 功能 | 状态 |
|---|---|---|
| 1 | ER 图（等宽 ASCII） | ✅ |
| 2 | 8 张概念卡（真实数据插值：会话数/query_source 计数/缓存命中率/工具调用数） | ✅ |
| 3 | 推理解释卡（要点列表 + 真实推理片段 400 字节选） | ✅ |
| 4 | turn 流程图 | ✅ |
| 5 | N+1 推理探测（web 串行 200 请求） | 单次 SQL（findReasoningSample） | ➕ |

## 后端数据能力（server/*.js → core/*）

| # | 能力 | 状态 |
|---|---|---|
| 1 | db.js 全部 18 个查询函数 | ✅ 逐条 SQL 对照 |
| 2 | 只读 + busy_timeout=5000 + 打开重试(50/150/400/1000ms×5) + 语句重试(30/60/120ms×4) + 损坏自愈 | ✅ |
| 3 | WAL 看门狗（5s 轮询，running→stopped 边沿 checkpoint TRUNCATE + 连接重建） | ✅ |
| 4 | Windows 进程探测 | Toolhelp32Snapshot（修掉 web 版恒 true 的 bug） | ➕ |
| 5 | transcript.js：uuid 尾部正则定位 + 全量解析 + categorize + summarize + aggregate | ✅ |
| 6 | log-tail.js：1MB 尾读 + 今天+昨天 trace 收集 + span 森林 | ✅（修 O(n²)/重复挂载） | 
| 7 | raw.js：19 表白名单 + 6 排序列白名单 + where 危险 token 拦截 | ✅ |
| 8 | JSON 列解析（message/part data） | ✅ QJsonDocument |

## Qt 版独有（web 版没有）

| # | 功能 |
|---|---|
| 1 | `--selftest` 无头数据层自检（21 项断言） |
| 2 | `--screenshot` 自动全页面截图导览（回归验证） |
| 3 | 离线图表（无 CDN 依赖） |
| 4 | 编译期类型安全 + 零依赖单 exe |
