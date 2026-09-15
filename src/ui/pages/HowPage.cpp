// HowPage.cpp — see HowPage.h. Static diagrams + live-data concept cards.

#include "HowPage.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QVBoxLayout>

#include "ui/Theme.h"
#include "ui/UiUtil.h"
#include "core/DbService.h"
#include "util/Async.h"
#include "util/Format.h"

using namespace UiUtil;

namespace {

const char* ER_DIAGRAM =
    "session (一次会话)\n"
    "  │  id · title · task_type · parent_id · directory · trace_id\n"
    "  │\n"
    "  ├──▶ message (一条消息: user 或 assistant)\n"
    "  │      │  data.role · data.modelID · data.mode · data.anchor.turnId\n"
    "  │      │\n"
    "  │      └──▶ part (消息的一部分: text / reasoning / tool / step-finish ...)\n"
    "  │             data.type 决定含义\n"
    "  │\n"
    "  ├──▶ turn_usage (一个回合的汇总)  ← 主键 (session_id, turn_id)\n"
    "  │      │\n"
    "  │      ├──▶ model_usage (单次模型调用)  ← turn_id 关联\n"
    "  │      │      input/output/reasoning_tokens · query_source · status\n"
    "  │      │\n"
    "  │      └──▶ tool_usage (单次工具调用)  ← turn_id 关联\n"
    "  │             tool_name · tool_call_id · status · read_only · destructive\n"
    "  │\n"
    "  ├──▶ session_entry (运行时事件: checkpoint / model_selection ...)\n"
    "  ├──▶ todo (TodoWrite 写入的任务)\n"
    "  └──▶ session_input (用户输入队列)\n"
    "\n"
    "关联键:\n"
    "  turn_id     串起 turn_usage ↔ model_usage ↔ tool_usage ↔ message.anchor\n"
    "  trace_id    贯穿 session ↔ *_usage ↔ 日志 JSONL ↔ transcript.jsonl\n"
    "  tool_call_id 连接 tool_usage ↔ part(type=tool).callID ↔ exec/<callId>-stdout.log";

const char* TURN_FLOW =
    "用户发消息\n"
    "   │\n"
    "   ▼\n"
    "turn_started (transcript) / message(role=user) 写入 SQLite\n"
    "   │\n"
    "   ▼\n"
    "model_request #1 ──▶ 发给 GLM-5.2（带完整历史 + 工具定义）\n"
    "   │                   │\n"
    "   │                   ├── model_network_status: started (sse, attempt 1)\n"
    "   │                   ├── model_streaming: reasoning_delta × N  ← 推理在这里流式产生\n"
    "   │                   ├── model_streaming: text_delta × N       ← 最终回答在这里流式产生\n"
    "   │                   └── model_network_status: completed\n"
    "   │\n"
    "   ▼\n"
    "model_complete: stopReason='tool-calls' (模型决定要调工具)\n"
    "   │\n"
    "   ▼\n"
    "tool.call: Bash / Read / Agent(...) ──▶ 执行 ──▶ tool.result (写入 part)\n"
    "   │   （如果是 Agent 工具，会派生子 agent → 新的 session + transcript）\n"
    "   │\n"
    "   ▼\n"
    "model_request #2 ──▶ 带上工具结果再问模型 …… (循环直到 stopReason='stop')\n"
    "   │\n"
    "   ▼\n"
    "turn_complete (transcript) / message(role=assistant) 写入 SQLite\n"
    "   │   turn_usage 汇总本轮所有 token / 工具 / 耗时";

QWidget* diagramBox(const char* text)
{
    auto* box = new QPlainTextEdit;
    box->setReadOnly(true);
    QFont f(QStringLiteral("Consolas"), 9);
    box->setFont(f);
    box->setPlainText(QString::fromUtf8(text));
    box->setLineWrapMode(QPlainTextEdit::NoWrap);
    box->setObjectName(QStringLiteral("diagram"));
    // tall enough to show the WHOLE diagram — a vertically-clipped ASCII
    // diagram inside its own scrollbar defeats its purpose
    QFontMetrics fm(f);
    const int lines = QString::fromUtf8(text).count(QLatin1Char('\n')) + 1;
    box->setFixedHeight(lines * fm.lineSpacing() + 28);
    return box;
}

QWidget* conceptCard(const QString& titleHtml, const QString& bodyHtml,
                     const QString& example)
{
    auto* card = new CardFrame;
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(14, 14, 14, 14);
    cl->setSpacing(7);
    auto* h = new QLabel(titleHtml);
    h->setProperty("cls", "how-title");
    cl->addWidget(h);
    auto* p = new QLabel(bodyHtml);
    p->setWordWrap(true);
    p->setTextFormat(Qt::RichText);
    p->setProperty("cls", "how-body");
    // the web version links to the sessions view from the body copy
    p->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    cl->addWidget(p);
    if (!example.isEmpty()) {
        auto* ex = new QLabel(example);
        ex->setWordWrap(true);
        ex->setTextFormat(Qt::RichText);
        ex->setProperty("cls", "example-box");
        cl->addWidget(ex);
    }
    return card;
}

} // namespace

HowPage::HowPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    // the reasoning explainer card + subtitle bake colors at build time —
    // refresh their rich-text colors on theme flips alongside the cards.
    // The intro label refresh runs even before first show (buildUi already
    // created it with the startup theme's colors).
    connect(&Theme::instance(), &Theme::changed, this, [this](bool) {
        if (m_reasonIntroLabel)
            m_reasonIntroLabel->setText(reasonIntroHtml());
        if (!m_loaded)
            return;
        // rebuild the concept cards with the new palette
        if (auto* l = m_conceptsHost->layout()) {
            while (l->count()) {
                auto* item = l->takeAt(0);
                if (item->widget())
                    item->widget()->deleteLater();
                delete item;
            }
        }
        loadConcepts();
        loadReasonExample();
    });
}

void HowPage::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    content->setMaximumWidth(1400);
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(24, 20, 24, 64);
    lay->setSpacing(0);
    auto* center = new QHBoxLayout;
    center->addStretch(1);
    center->addWidget(content, 10);
    center->addStretch(1);
    auto* centerHost = new QWidget;
    centerHost->setLayout(center);
    scroll->setWidget(centerHost);
    outer->addWidget(scroll);

    auto* h1 = new QLabel(QStringLiteral("ZCode 运行原理"));
    h1->setObjectName(QStringLiteral("h1"));
    lay->addWidget(h1);
    auto* sub = new QLabel(QStringLiteral(
        "用你自己的真实数据解释：一次 agent 运行里发生了什么、推理是怎么进行的、token 花在哪。"));
    sub->setProperty("cls", "muted");
    sub->setStyleSheet("font-size:9pt;");
    lay->addWidget(sub);

    lay->addWidget(sectionLabel(QStringLiteral("数据模型"),
                                 QStringLiteral("一次运行产生的层级关系")));
    lay->addWidget(diagramBox(ER_DIAGRAM));

    lay->addWidget(sectionLabel(QStringLiteral("核心概念"),
                                 QStringLiteral("用你机器上的真实例子")));
    m_conceptsHost = new QWidget;
    auto* cgl = new QGridLayout(m_conceptsHost);
    cgl->setContentsMargins(0, 0, 0, 0);
    cgl->setSpacing(12);
    lay->addWidget(m_conceptsHost);

    // ── reasoning explainer ──
    lay->addWidget(sectionLabel(QStringLiteral("推理（reasoning）是怎么发生的"),
                                 QStringLiteral("你重点关注的部分")));
    {
        auto* card = new CardFrame;
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(14, 14, 14, 14);
        cl->setSpacing(10);
        auto* p = new QLabel(reasonIntroHtml());
        p->setTextFormat(Qt::RichText);
        p->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        connect(p, &QLabel::linkActivated, this,
                [this](const QString& href) { emit navigateRequested(href); });
        m_reasonIntroLabel = p;
        cl->addWidget(p);
        m_reasonExample = new QLabel;
        m_reasonExample->setWordWrap(true);
        m_reasonExample->setTextFormat(Qt::PlainText);
        m_reasonExample->setProperty("cls", "example-box");
        cl->addWidget(m_reasonExample);
        lay->addWidget(card);
    }

    lay->addWidget(sectionLabel(QStringLiteral("一次 turn 的完整流程"),
                                 QStringLiteral("turn → model_request → tools → model_complete")));
    lay->addWidget(diagramBox(TURN_FLOW));
    lay->addStretch(1);
}

void HowPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (!m_loaded) {
        loadConcepts();
        loadReasonExample();
        m_loaded = true;
    }
}

QString HowPage::reasonIntroHtml() const
{
    const Palette& pal = Theme::instance().pal();
    return QStringLiteral(
        "<p style='margin:0 0 10px;color:%1;font-size:10pt'>GLM-5.2 等"
        "\"思考型\"模型在给出最终回答前，会先输出一段<b style='color:%2'>推理过程"
        "（chain-of-thought）</b>。ZCode 把它单独记录，与最终输出分开：</p>"
        "<ul style='color:%3;font-size:9.5pt;line-height:1.8;margin:0;padding-left:18px'>"
        "<li><code>part</code> 表里 <code>type='reasoning'</code> 的行 = 推理原文（思考链）</li>"
        "<li><code>part</code> 表里 <code>type='text'</code> 的行 = 最终回答</li>"
        "<li><code>model_usage.reasoning_tokens</code> = 推理消耗的 token"
        "（计入成本，但用户看不到这段文字）</li>"
        "<li>在 <a href='sessions'>会话详情 → Context</a> 标签里，每条推理会以紫色侧边块呈现，点击可展开看全文</li>"
        "<li>在子 agent 的 Timeline 里，<code>reasoning_delta</code> 流式事件被折叠成"
        " <code>◆ think</code> 行，显示推理字符量</li></ul>")
        .arg(pal.fg1.name(), pal.accent2.name(), pal.fg2.name());
}

void HowPage::loadConcepts()
{
    // overview + agents forest + reasoning sample in one pool trip
    struct HowData {
        types::OverviewData ov;
        QVector<types::AgentNode> forest;
        QString reasoning;
    };
    Async::run<HowData>(
        this,
        []() {
            auto& db = DbService::instance();
            HowData d;
            d.ov = db.overview(QStringLiteral("24h"));
            d.forest = db.agentsForest();
            d.reasoning = db.findReasoningSample();
            return d;
        },
        [this](const HowData& d) { applyConcepts(d.ov, d.forest); applyReason(d.reasoning); });
}

void HowPage::applyConcepts(const types::OverviewData& ov, const QVector<types::AgentNode>& forest)
{
    const types::Kpis& k = ov.kpis;

    int totalSessions = 0;
    std::function<int(const QVector<types::AgentNode>&)> count =
        [&](const QVector<types::AgentNode>& nodes) -> int {
        int n = int(nodes.size());
        for (const types::AgentNode& x : nodes)
            n += count(x.children);
        return n;
    };
    totalSessions = count(forest);

    // query_source counts
    QHash<QString, qint64> srcMap;
    for (const types::ModelBreakdown& m : ov.byModel)
        srcMap[m.querySource] += m.calls;
    QStringList srcList;
    {
        QVector<QPair<QString, qint64>> srcs;
        for (auto it = srcMap.constBegin(); it != srcMap.constEnd(); ++it)
            srcs.push_back({it.key(), it.value()});
        std::sort(srcs.begin(), srcs.end(),
                  [](const QPair<QString, qint64>& a, const QPair<QString, qint64>& b) {
                      return a.second > b.second;
                  });
        for (const auto& [name, n] : srcs)
            srcList << QStringLiteral("<code>%1</code>(%2)").arg(
                name.toHtmlEscaped(), Format::fmtInt(double(n)));
    }
    const auto srcCount = [&](const QString& name) {
        return Format::fmtInt(double(srcMap.value(name)));
    };

    const int cacheRate = k.inTok
        ? int(qMin<qint64>(100, k.cacheRead * 100 / k.inTok)) : 0;

    const auto code = [](const QString& s) {
        return QStringLiteral("<code>%1</code>").arg(s.toHtmlEscaped());
    };

    const QList<QPair<QString, QPair<QString, QString>>> concepts = {
        {QStringLiteral("Session（会话）"),
         {QStringLiteral(
              "一次独立的对话。分三类：<code>interactive</code>（你直接聊的主会话）、"
              "<code>subagent_child</code>（主 agent 派生的子 agent，做搜索/调研等只读活）、"
              "<code>selection_side_chat</code>（选中代码的侧边提问）。你的库里有 <b>%1</b> 个会话，"
              "其中 %2 个主会话派生了大量子 agent。")
              .arg(Format::fmtInt(double(totalSessions)),
                   Format::fmtInt(double(forest.size()))),
          QStringLiteral("例：最近的主会话 \"查看和观测 zcode agent\" 派生了 4 个 Explore 子 agent（在「子 Agent」页可见调用树）")}},
        {QStringLiteral("Turn（回合）"),
         {QStringLiteral(
              "你发一条消息 → agent 完整回复一次，中间可能调多次模型、跑多个工具，"
              "这整个过程是一个 turn。一个 turn = 多个 model_request + 多个 tool_call。"
              "24h 内有 <b>%1</b> 个活跃会话在产生 turn。")
              .arg(Format::fmtInt(double(k.activeSessions))),
          QStringLiteral("看「会话 → Turns」标签：每行是一个 turn，横条长度=耗时，能看到模型请求次数、工具调用数、token 消耗")}},
        {QStringLiteral("query_source（请求来源）"),
         {QStringLiteral(
              "区分这次模型调用是为什么：%1。<b>main_turn</b> 是真正回答你的；"
              "<b>subagent</b> 是子 agent 干活；<b>compact</b> 是上下文压缩；"
              "<b>session_title</b> 只是给会话起个标题（很便宜）。")
              .arg(srcList.join(QStringLiteral("、"))),
          QStringLiteral("24h 内：main_turn %1 次、subagent %2 次、标题生成 %3 次")
              .arg(srcCount(QStringLiteral("main_turn")), srcCount(QStringLiteral("subagent")),
                   srcCount(QStringLiteral("session_title")))}},
        {QStringLiteral("mode（运行模式）"),
         {QStringLiteral(
              "<b>yolo</b> = 自由执行（默认）、<b>plan</b> = 先出方案再实施、"
              "<b>build</b> = 实施模式。影响 agent 的自主程度和是否需要确认。"),
          QStringLiteral("mode 存在 message.data.mode 字段，在 Context 标签的每条 assistant 消息头部可见")}},
        {QStringLiteral("context compaction（上下文压缩）"),
         {QStringLiteral(
              "对话太长时（接近模型上下文窗口），ZCode 自动把历史压缩成摘要，腾出空间继续。"
              "这是为什么你能跟 agent 聊很久而不爆 token。"),
          QStringLiteral("看「会话 → Timeline/Context」里的 ⌘ compaction 行：会显示 pre/post token 数，比如 95393 → 6035")}},
        {QStringLiteral("prompt cache（提示缓存）"),
         {QStringLiteral(
              "系统提示、工具定义、历史消息会被缓存，下次请求命中缓存就不重新计费。"
              "你的缓存命中率：<b style='color:%1'>%2%</b>（输入 token 里被缓存命中的比例）。"
              "这是省成本的关键。")
              .arg(Theme::instance().pal().sevOk.name())
              .arg(cacheRate),
          QStringLiteral("24h 内：输入 %1 token，其中 %2 来自缓存读取")
              .arg(Format::fmtNum(double(k.inTok)), Format::fmtNum(double(k.cacheRead)))}},
        {QStringLiteral("tool 调用（工具）"),
         {QStringLiteral(
              "agent 通过工具与外界交互：<code>Bash</code> 跑命令、<code>Read/Edit/Write</code> "
              "改文件、<code>Grep/Glob</code> 搜索、<code>Agent</code> 派生子 agent。"
              "每个工具有 <b>readOnly/destructive/sideEffectScope</b> 安全标记。"),
          QStringLiteral("24h 内工具调用 %1 次，失败 %2 次。在 Timeline 里 tool.call/tool.result 行可见")
              .arg(Format::fmtInt(double(k.toolCalls)), Format::fmtInt(double(k.toolErrors)))}},
        {QStringLiteral("MCP 工具"),
         {QStringLiteral(
              "名字以 <code>mcp__</code> 开头的是外部 MCP 服务器提供的工具"
              "（如 gitnexus、agentmemory、chrome-devtools）。你装了 3 个 MCP 服务器，"
              "扩展了 agent 的能力。"),
          QStringLiteral("在「实时监控」按工具表里能看到 mcp__gitnexus__query 等的调用频次")}},
    };

    auto* gl = qobject_cast<QGridLayout*>(m_conceptsHost->layout());
    if (!gl)
        return;
    int row = 0;
    int col = 0;
    for (const auto& [title, body] : concepts) {
        gl->addWidget(conceptCard(title, body.first, body.second), row, col);
        if (++col >= 2) {
            col = 0;
            ++row;
        }
    }
}

void HowPage::loadReasonExample()
{
    // reasoning sample was already fetched by loadConcepts' async run;
    // this entry point only re-applies (theme flips call it after rebuilds)
    if (m_reasonText.isEmpty()) {
        m_reasonExample->setText(
            QStringLiteral("暂无 reasoning 记录（当前会话未启用思考，或已被清理）"));
        return;
    }
    QString text = m_reasonText;
    if (text.size() > 400)
        text = text.left(400) + QStringLiteral("…");
    m_reasonExample->setText(
        QStringLiteral("来自你机器的真实推理片段（节选）：\n") + text);
}

void HowPage::applyReason(const QString& sample)
{
    m_reasonText = sample; // cached for theme-flip re-application
    loadReasonExample();
}
