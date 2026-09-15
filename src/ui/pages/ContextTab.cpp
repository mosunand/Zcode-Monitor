// ContextTab.cpp — see ContextTab.h.

#include "ContextTab.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMouseEvent>
#include <QListWidget>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

#include "ui/Theme.h"
#include "ui/UiUtil.h"
#include "core/DbService.h"
#include "util/Async.h"
#include "util/Format.h"

using namespace UiUtil;

namespace {

QString js(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : QString();
}

double jnum(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : qQNaN();
}

// first text part of a message (web firstText)
QString firstText(const types::Message& m)
{
    QStringList parts;
    for (const types::Part& p : m.parts) {
        if (p.data.value(QLatin1String("type")).toString() == QLatin1String("text")) {
            const QString t = js(p.data, "text");
            if (!t.isEmpty())
                parts << t;
        }
    }
    return parts.join(QLatin1Char(' ')).trimmed();
}

// strip agent-injected XML-ish wrappers (web cleanText: both open and close tags)
QString cleanText(const QString& s)
{
    static const QRegularExpression openTag(QStringLiteral("<[a-zA-Z][^>]*>"));
    static const QRegularExpression closeTag(QStringLiteral("</[a-zA-Z][^>]*>"));
    QString t = s;
    t.replace(openTag, QStringLiteral(" "));
    t.replace(closeTag, QStringLiteral(" "));
    t.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return t.trimmed();
}

// generic JSON value → pretty text
QString prettyValue(const QJsonValue& v)
{
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented));
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented));
    if (v.isString())
        return v.toString();
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble())
        return QString::number(v.toDouble());
    return QString();
}

// frame whose header click toggles a body; runs a one-shot callback the
// first time it expands (used to lazily load tool exec output)
class CollapsiblePart : public QFrame {
public:
    explicit CollapsiblePart(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }
    void setExpanded(bool e)
    {
        m_expanded = e;
        setProperty("expanded", e); // survives findChildren() state capture
        if (m_body)
            m_body->setVisible(e);
        if (e && !m_firstExpanded) {
            m_firstExpanded = true;
            if (m_onFirstExpand)
                m_onFirstExpand();
        }
    }
    bool isExpanded() const { return m_expanded; }
    void setBody(QWidget* body) { m_body = body; }
    void setOnFirstExpand(std::function<void()> fn) { m_onFirstExpand = std::move(fn); }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        QFrame::mousePressEvent(e);
        if (e->button() == Qt::LeftButton)
            setExpanded(!m_expanded);
    }

private:
    QWidget* m_body = nullptr;
    bool m_expanded = false;
    bool m_firstExpanded = false;
    std::function<void()> m_onFirstExpand;
};

QLabel* monoLabel(const QString& text, const QColor& color, bool wrap = true)
{
    auto* l = new QLabel(text);
    l->setWordWrap(wrap);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral("font-family:'Consolas';font-size:8pt;color:%1;")
                         .arg(color.name()));
    return l;
}

// number of collapsible parts (reasoning/tool) inside a message widget —
// same order as the m_expanders lambdas pushed while building that message
int countCollapsibles(QWidget* msgWidget)
{
    int n = 0;
    const auto parts = msgWidget->findChildren<QFrame*>();
    for (QFrame* part : parts) {
        const QString cls = part->property("cls").toString();
        if (cls == QLatin1String("reasoning-part") || cls == QLatin1String("tool-part"))
            ++n;
    }
    return n;
}

} // namespace

ContextTab::ContextTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    // theme flip: re-render the same data, then restore the user's scroll
    // position and per-part expand/collapse state (web keeps the DOM and
    // swaps CSS variables; we rebuild widgets, so state must round-trip)
    connect(&Theme::instance(), &Theme::changed, this, [this](bool) {
        if (m_sessionId.isEmpty())
            return;
        const int scrollPos = m_scroll->verticalScrollBar()->value();
        // identity of an open part: message index + n-th collapsible in it
        QSet<QString> openKeys;
        for (int i = 0; i < m_msgWidgets.size(); ++i) {
            if (!m_msgWidgets.value(i))
                continue;
            const auto parts = m_msgWidgets[i]->findChildren<QFrame*>();
            int nth = 0;
            for (QFrame* part : parts) {
                const QString cls = part->property("cls").toString();
                if (cls != QLatin1String("reasoning-part")
                    && cls != QLatin1String("tool-part"))
                    continue;
                if (part->property("expanded").toBool())
                    openKeys.insert(QStringLiteral("%1:%2").arg(i).arg(nth));
                ++nth;
            }
        }
        m_restoreExpanded = openKeys;
        load(m_sessionId);
        m_restoreExpanded.clear();
        m_scroll->verticalScrollBar()->setValue(scrollPos);
    });
}

void ContextTab::buildUi()
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 16);
    lay->setSpacing(14);

    // ── left rail (one node per turn) ──
    auto* railHost = new QWidget;
    railHost->setFixedWidth(220);
    auto* rl = new QVBoxLayout(railHost);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    auto* railTools = new QWidget;
    auto* rtl = new QHBoxLayout(railTools);
    rtl->setContentsMargins(0, 2, 0, 8);
    rtl->setSpacing(6);
    auto* expandBtn = new QPushButton(QStringLiteral("展开全部"));
    auto* collapseBtn = new QPushButton(QStringLiteral("收拢全部"));
    expandBtn->setCursor(Qt::PointingHandCursor);
    collapseBtn->setCursor(Qt::PointingHandCursor);
    connect(expandBtn, &QPushButton::clicked, this, [this] { setAllExpanded(true); });
    connect(collapseBtn, &QPushButton::clicked, this, [this] { setAllExpanded(false); });
    rtl->addWidget(expandBtn, 1);
    rtl->addWidget(collapseBtn, 1);
    rl->addWidget(railTools);

    m_rail = new QListWidget;
    m_rail->setUniformItemSizes(false);
    m_rail->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_rail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_rail, &QListWidget::currentRowChanged, this,
            [this](int row) { if (row >= 0) scrollToTurn(row); });
    rl->addWidget(m_rail, 1);
    lay->addWidget(railHost);

    // ── right conversation stream ──
    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_convHost = new QWidget;
    m_convLay = new QVBoxLayout(m_convHost);
    m_convLay->setContentsMargins(0, 0, 4, 0);
    m_convLay->setSpacing(12);
    m_convLay->addStretch(1);
    m_scroll->setWidget(m_convHost);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { updateRailHighlight(); });
    lay->addWidget(m_scroll, 1);
}

void ContextTab::load(const QString& sessionId)
{
    m_sessionId = sessionId;
    // 800 messages + their parts decode a lot of JSON — load on a pool
    // thread, apply on the GUI thread. Sequence guard drops results from a
    // session the user has already navigated away from.
    const QString sid = sessionId;
    const int seq = ++m_requestSeq;
    struct ConvResult {
        QVector<types::Message> messages;
        QVector<types::TurnRow> turns;
    };
    Async::run<ConvResult>(
        this,
        [sid]() {
            auto& db = DbService::instance();
            ConvResult r;
            r.messages = db.sessionConversation(sid, 800);
            r.turns = db.sessionTurns(sid);
            return r;
        },
        [this, seq, sid](const ConvResult& r) {
            if (seq != m_requestSeq || sid != m_sessionId)
                return; // superseded
            if (r.messages.isEmpty()) {
                m_rail->clear();
                m_msgWidgets.clear();
                m_msgTurnIdx.clear();
                m_expanders.clear();
                while (m_convLay->count() > 1) {
                    auto* item = m_convLay->takeAt(0);
                    if (item->widget())
                        item->widget()->deleteLater();
                    delete item;
                }
                auto* empty = new QLabel(QStringLiteral("无对话记录"));
                empty->setAlignment(Qt::AlignCenter);
                empty->setProperty("cls", "faint");
                empty->setStyleSheet("padding:28px;");
                m_convLay->insertWidget(0, empty);
                return;
            }
            render(r.messages, r.turns);
        });
}

void ContextTab::render(const QVector<types::Message>& messages,
                        const QVector<types::TurnRow>& turns)
{
    const Palette& pal = Theme::instance().pal();

    // ── turn grouping (port of the sessions.js algorithm) ──
    QStringList turnOrder;
    QHash<QString, QString> turnUser;
    QHash<QString, QString> turnFallback;
    m_msgTurnIdx.assign(int(messages.size()), -1);
    QVector<int> pending;
    for (int i = 0; i < messages.size(); ++i) {
        const QString tid = messages[i].turnId;
        if (tid.isEmpty()) {
            pending.push_back(i);
            continue;
        }
        if (!turnOrder.contains(tid)) {
            turnOrder << tid;
            turnUser.insert(tid, QString());
            turnFallback.insert(tid, QString());
        }
        const int ix = turnOrder.indexOf(tid);
        m_msgTurnIdx[i] = ix;
        for (int pi : pending)
            m_msgTurnIdx[pi] = ix;
        pending.clear();
        const QString txt = firstText(messages[i]);
        if (txt.isEmpty())
            continue;
        if (messages[i].role == QLatin1String("user") && turnUser.value(tid).isEmpty())
            turnUser[tid] = txt;
        else if (messages[i].role == QLatin1String("assistant")
                 && turnFallback.value(tid).isEmpty())
            turnFallback[tid] = txt;
    }
    // trailing orphans (lifecycle events after the last turn) → last turn
    if (!pending.isEmpty() && !turnOrder.isEmpty()) {
        const int lastIx = turnOrder.size() - 1;
        for (int pi : pending)
            m_msgTurnIdx[pi] = lastIx;
    }

    const auto pickSummary = [&](const QString& tid, int idx) {
        const QString u = cleanText(turnUser.value(tid));
        if (u.size() >= 2)
            return u;
        const QString a = cleanText(turnFallback.value(tid));
        if (a.size() >= 2)
            return a;
        return idx > 0 ? QStringLiteral("Turn %1").arg(idx + 1)
                       : QStringLiteral("Turn 1");
    };

    QHash<QString, types::TurnRow> turnMeta;
    for (const types::TurnRow& t : turns)
        turnMeta.insert(t.turnId, t);

    // ── rail ──
    m_rail->blockSignals(true);
    m_rail->clear();
    for (int idx = 0; idx < turnOrder.size(); ++idx) {
        const QString tid = turnOrder.at(idx);
        const types::TurnRow meta = turnMeta.value(tid);
        const QString summary = pickSummary(tid, idx);
        const QString shortS = summary.size() > 40
            ? summary.left(40) + QStringLiteral("…") : summary;

        auto* node = new QWidget;
        auto* nl = new QHBoxLayout(node);
        nl->setContentsMargins(0, 7, 0, 7);
        nl->setSpacing(9);
        auto* dot = new Dot(9);
        dot->setColor(meta.status == QLatin1String("error")
                          ? pal.sevErr
                          : (meta.status == QLatin1String("cancelled") ? pal.sevWarn
                                                                        : pal.sevOk));
        nl->addWidget(dot);
        auto* text = new QWidget;
        auto* tl = new QVBoxLayout(text);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(2);
        auto* sum = new QLabel(shortS);
        sum->setWordWrap(true);
        sum->setToolTip(summary);
        sum->setStyleSheet(
            QStringLiteral("color:%1;font-size:9pt;").arg(pal.fg2.name()));
        tl->addWidget(sum);
        QStringList sub;
        if (meta.toolCallCount > 0)
            sub << QStringLiteral("⚙×") + QString::number(meta.toolCallCount);
        if (meta.computedTotalTokens > 0)
            sub << Format::fmtNum(double(meta.computedTotalTokens)) + QStringLiteral(" tok");
        if (meta.durationOk)
            sub << Format::fmtDur(double(meta.durationMs) / 1000.0);
        if (!sub.isEmpty()) {
            auto* subL = new QLabel(sub.join(QStringLiteral(" · ")));
            subL->setStyleSheet(QStringLiteral("color:%1;font-size:7.5pt;")
                                    .arg(pal.fg4.name()));
            tl->addWidget(subL);
        }
        nl->addWidget(text, 1);

        auto* item = new QListWidgetItem(m_rail);
        item->setData(Qt::UserRole, idx);
        item->setSizeHint(node->sizeHint().expandedTo(QSize(200, 40)));
        m_rail->setItemWidget(item, node);
    }
    m_rail->blockSignals(false);

    // ── conversation body ──
    while (m_convLay->count() > 1) {
        auto* item = m_convLay->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_msgWidgets.clear();
    m_expanders.clear();
    for (int i = 0; i < messages.size(); ++i)
        m_msgWidgets.push_back(buildMessageWidget(messages[i], i));

    // Restore expand state captured before a theme re-render. Identity is
    // "msgIndex:n-th collapsible part"; m_expanders were pushed in exactly
    // that order (one per collapsible part, message by message), so we can
    // walk both in lockstep.
    if (!m_restoreExpanded.isEmpty()) {
        int expIdx = 0;
        for (int i = 0; i < m_msgWidgets.size() && expIdx < m_expanders.size(); ++i) {
            const int nth =
                m_msgWidgets.value(i) ? countCollapsibles(m_msgWidgets[i]) : 0;
            for (int p = 0; p < nth && expIdx < m_expanders.size(); ++p, ++expIdx) {
                if (m_restoreExpanded.contains(
                        QStringLiteral("%1:%2").arg(i).arg(p)))
                    m_expanders[expIdx](true);
            }
        }
    }
}

QWidget* ContextTab::buildMessageWidget(const types::Message& m, int msgIndex)
{
    const Palette& pal = Theme::instance().pal();

    auto* frame = new QFrame;
    frame->setProperty("cls", "msg-frame");
    auto* fl = new QVBoxLayout(frame);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(0);

    // header: #num, role, meta, time
    auto* head = new QWidget;
    head->setProperty("cls", "msg-head");
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(11, 7, 11, 7);
    hl->setSpacing(10);
    auto* num = new QLabel(QStringLiteral("#%1").arg(msgIndex + 1));
    num->setProperty("cls", "msg-num");
    hl->addWidget(num);
    auto* role = new QLabel(m.role.isEmpty() ? QStringLiteral("?") : m.role.toUpper());
    role->setProperty("cls", m.role == QLatin1String("user") ? "role-user"
                                                             : "role-assistant");
    hl->addWidget(role);

    QStringList meta;
    if (!m.model.isEmpty())
        meta << m.model;
    if (!m.variant.isEmpty())
        meta << QStringLiteral("variant=") + m.variant;
    if (!m.mode.isEmpty())
        meta << m.mode;
    if (!m.agent.isEmpty())
        meta << m.agent;
    if (!m.turnId.isEmpty())
        meta << QStringLiteral("turn ") + m.turnId.left(10);
    if (!m.tokens.isEmpty()) {
        QStringList tok;
        if (m.tokens.contains(QLatin1String("input")))
            tok << QStringLiteral("in ") + Format::fmtNum(jnum(m.tokens, "input"));
        if (m.tokens.contains(QLatin1String("output")))
            tok << QStringLiteral("out ") + Format::fmtNum(jnum(m.tokens, "output"));
        if (m.tokens.value(QLatin1String("reasoning")).isDouble())
            tok << QStringLiteral("think ")
                     + Format::fmtNum(jnum(m.tokens, "reasoning"));
        if (!tok.isEmpty())
            meta << tok.join(QStringLiteral(" / "));
    }
    if (!meta.isEmpty()) {
        auto* metaL = new QLabel(meta.join(QStringLiteral(" · ")));
        metaL->setProperty("cls", "mono-fg4");
        hl->addWidget(metaL, 1);
    } else {
        hl->addStretch(1);
    }
    auto* time = new QLabel(Format::fmtTime(m.timeCreatedMs));
    time->setProperty("cls", "mono-fg4");
    hl->addWidget(time);
    fl->addWidget(head);

    // body: parts
    auto* body = new QWidget;
    body->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(12, 9, 12, 9);
    bl->setSpacing(9);
    if (m.parts.isEmpty()) {
        auto* none = new QLabel(QStringLiteral("(无内容)"));
        none->setStyleSheet(
            QStringLiteral("color:%1;font-size:9pt;").arg(pal.fg4.name()));
        bl->addWidget(none);
    }
    for (const types::Part& p : m.parts)
        bl->addWidget(buildPartWidget(p));
    fl->addWidget(body);
    m_convLay->insertWidget(m_convLay->count() - 1, frame);
    return frame;
}

QWidget* ContextTab::buildPartWidget(const types::Part& p)
{
    const Palette& pal = Theme::instance().pal();
    const QJsonObject& d = p.data;
    const QString t = js(d, "type");

    if (t == QLatin1String("text")) {
        auto* text = new QLabel(js(d, "text"));
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        text->setProperty("cls", "body-text");
        return text;
    }
    if (t == QLatin1String("reasoning")) {
        auto* part = new CollapsiblePart;
        part->setProperty("cls", "reasoning-part");
        auto* pl = new QVBoxLayout(part);
        pl->setContentsMargins(11, 8, 11, 8);
        pl->setSpacing(3);
        auto* label = new QLabel(QStringLiteral("◆ 推理思考 (reasoning) — 点击展开"));
        label->setProperty("cls", "think-label");
        pl->addWidget(label);
        auto* text = new QLabel(js(d, "text"));
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        text->setProperty("cls", "reason-text");
        pl->addWidget(text);
        part->setBody(text);
        part->setExpanded(false);
        m_expanders.push_back([part](bool e) { part->setExpanded(e); });
        return part;
    }
    if (t == QLatin1String("tool")) {
        const QJsonObject st = d.value(QLatin1String("state")).toObject();
        const QString toolName = js(d, "tool").isEmpty() ? QStringLiteral("?")
                                                          : js(d, "tool");
        const QString callId = js(d, "callID");

        auto* part = new CollapsiblePart;
        part->setProperty("cls", "tool-part");
        auto* pl = new QVBoxLayout(part);
        pl->setContentsMargins(11, 7, 11, 7);
        pl->setSpacing(7);

        auto* labelRow = new QWidget;
        auto* lrl = new QHBoxLayout(labelRow);
        lrl->setContentsMargins(0, 0, 0, 0);
        lrl->setSpacing(8);
        auto* lbl = new QLabel(QStringLiteral("⚙ ") + toolName);
        lbl->setProperty("cls", "tool-label");
        lrl->addWidget(lbl);
        lrl->addWidget(new BadgeLabel(js(st, "status"),
                                      statusBadgeKind(js(st, "status"))));
        if (!js(st, "title").isEmpty()) {
            auto* title = new QLabel(js(st, "title"));
            title->setStyleSheet(
                QStringLiteral("color:%1;font-size:8pt;").arg(pal.fg4.name()));
            lrl->addWidget(title, 1);
        } else {
            lrl->addStretch(1);
        }
        pl->addWidget(labelRow);

        auto* toolBody = new QWidget;
        auto* tbl = new QVBoxLayout(toolBody);
        tbl->setContentsMargins(0, 0, 0, 0);
        tbl->setSpacing(5);
        const QJsonValue input = st.value(QLatin1String("input"));
        if (!input.isUndefined() && !input.isNull() && !prettyValue(input).isEmpty()) {
            auto* inLabel = new QLabel(QStringLiteral("input:"));
            inLabel->setProperty("cls", "muted");
            inLabel->setStyleSheet("font-size:8pt;");
            tbl->addWidget(inLabel);
            tbl->addWidget(monoLabel(prettyValue(input), pal.fg2));
        }
        auto* outLabel = new QLabel(QStringLiteral("output: (加载中或无 exec 记录)"));
        outLabel->setWordWrap(true);
        outLabel->setProperty("cls", "plain-mono-fg4");
        tbl->addWidget(outLabel);

        // lazy load exec output on first expand — async (files can be 200KB)
        if (!callId.isEmpty()) {
            const QString sid = m_sessionId;
            const int seq = m_requestSeq;
            QPointer<QLabel> outPtr(outLabel);
            part->setOnFirstExpand([this, sid, callId, outPtr, seq]() {
                Q_UNUSED(outPtr);
                Async::run<types::ToolOutput>(
                    this,
                    [sid, callId]() { return DbService::instance().toolOutput(sid, callId); },
                    [this, outPtr, seq](const types::ToolOutput& out) {
                        // requestSeq moved on → the tab was reloaded; the
                        // label we captured belongs to the old build
                        if (seq != m_requestSeq || outPtr.isNull())
                            return;
                        if (!out.stdout_.isEmpty() || !out.stderr_.isEmpty()) {
                            QString text;
                            if (!out.stdout_.isEmpty())
                                text += QStringLiteral("stdout:\n") + out.stdout_;
                            if (!out.stderr_.isEmpty())
                                text += (text.isEmpty() ? QString() : QStringLiteral("\n"))
                                        + QStringLiteral("stderr:\n") + out.stderr_;
                            outPtr->setText(text);
                            outPtr->setProperty("cls", "plain-mono-fg1");
                            outPtr->style()->unpolish(outPtr.data());
                            outPtr->style()->polish(outPtr.data());
                        }
                    });
            });
        }

        part->setBody(toolBody);
        part->setExpanded(false);
        pl->addWidget(toolBody);
        m_expanders.push_back([part](bool e) { part->setExpanded(e); });
        return part;
    }
    if (t == QLatin1String("step-finish")) {
        const QJsonObject tk = d.value(QLatin1String("tokens")).toObject();
        const QJsonObject cache = tk.value(QLatin1String("cache")).toObject();
        auto* sf = new QLabel(QStringLiteral(
            "step-finish · %1 · in %2 / out %3 / think %4 · cache r%5 w%6")
            .arg(js(d, "reason"),
                 Format::fmtNum(jnum(tk, "input")),
                 Format::fmtNum(jnum(tk, "output")),
                 Format::fmtNum(jnum(tk, "reasoning")),
                 Format::fmtNum(jnum(cache, "read")),
                 Format::fmtNum(jnum(cache, "write"))));
        sf->setStyleSheet(QStringLiteral("font-family:'Consolas';font-size:8pt;color:%1;"
                                           "border-top:1px dashed %2;")
                              .arg(pal.fg4.name(), pal.borderSoft.name()));
        return sf;
    }
    if (t == QLatin1String("timeline")) {
        const QJsonObject from = d.value(QLatin1String("fromModel")).toObject();
        const QJsonObject to = d.value(QLatin1String("toModel")).toObject();
        auto* tl = new QLabel(QStringLiteral("⎯ %1 %2 → %3 %4")
                                  .arg(js(d, "timelineType"), js(from, "modelID"),
                                       js(to, "modelID"), js(to, "variant")));
        tl->setStyleSheet(QStringLiteral("font-family:'Consolas';font-size:8pt;"
                                           "border-top:1px dashed %1;color:%2;")
                              .arg(pal.borderSoft.name(), pal.fg4.name()));
        return tl;
    }
    if (t == QLatin1String("compaction")) {
        auto* cp = new QLabel(QStringLiteral("⌘ compaction (%1) · %2 → %3 tok")
                                  .arg(js(d, "trigger"),
                                       Format::fmtNum(jnum(d, "preCompactTokenCount")),
                                       Format::fmtNum(jnum(d, "postCompactTokenCount"))));
        cp->setStyleSheet(QStringLiteral("font-family:'Consolas';font-size:8pt;"
                                           "border-top:1px dashed %1;color:%2;")
                              .arg(pal.borderSoft.name(), pal.fg4.name()));
        return cp;
    }
    if (t == QLatin1String("file")) {
        const QString mime = js(d, "mime");
        const QString url = js(d, "url");
        if (mime.startsWith(QLatin1String("image/")) && !url.isEmpty()) {
            auto* imgLabel = new QLabel;
            const QString local = url.startsWith(QLatin1String("file:"))
                ? QUrl(url).toLocalFile() : url;
            QImage img(local);
            if (!img.isNull()) {
                imgLabel->setPixmap(QPixmap::fromImage(
                    img.scaled(640, 640, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            } else {
                imgLabel->setText(url);
            }
            return imgLabel;
        }
        auto* fl2 = new QLabel(QStringLiteral("📎 file · %1 · %2").arg(mime, url));
        fl2->setWordWrap(true);
        fl2->setStyleSheet(
            QStringLiteral("font-family:'Consolas';font-size:8pt;color:%1;")
                .arg(pal.fg4.name()));
        return fl2;
    }
    auto* other = new QLabel(QStringLiteral("[%1]").arg(t.isEmpty()
        ? QStringLiteral("part") : t));
    other->setStyleSheet(QStringLiteral("color:%1;font-size:8pt;").arg(pal.fg4.name()));
    return other;
}

void ContextTab::scrollToTurn(int turnIdx)
{
    for (int i = 0; i < m_msgWidgets.size(); ++i) {
        if (m_msgTurnIdx.value(i) == turnIdx && m_msgWidgets[i]) {
            const int target = m_msgWidgets[i]->y();
            auto* bar = m_scroll->verticalScrollBar();
            auto* anim = new QPropertyAnimation(bar, QByteArrayLiteral("value"), this);
            anim->setDuration(200);
            anim->setStartValue(bar->value());
            anim->setEndValue(qMax(0, target - 4));
            anim->start(QAbstractAnimation::DeleteWhenStopped);
            return;
        }
    }
}

void ContextTab::updateRailHighlight()
{
    // pick the turn with the largest overlap in the top-40% band of the
    // viewport (web IntersectionObserver, rootMargin 0 0 -60% 0); ties → earliest
    const int viewTop = m_scroll->verticalScrollBar()->value();
    const int bandBottom = viewTop + qMax(int(m_scroll->viewport()->height() * 0.4), 40);

    int best = -1;
    double bestRatio = 0;
    for (int i = 0; i < m_msgWidgets.size(); ++i) {
        QWidget* w = m_msgWidgets[i];
        if (!w || m_msgTurnIdx.value(i) < 0)
            continue;
        const int top = w->y();
        const int bottom = top + w->height();
        const int overlap = qMin(bottom, bandBottom) - qMax(top, viewTop);
        if (overlap <= 0)
            continue;
        const double ratio =
            qMin(1.0, double(overlap) / double(qMax(1, w->height())));
        const int turnIdx = m_msgTurnIdx[i];
        if (ratio > bestRatio || (ratio == bestRatio && best >= 0 && turnIdx < best)) {
            best = turnIdx;
            bestRatio = ratio;
        }
    }
    if (best >= 0 && m_rail->currentRow() != best) {
        m_rail->blockSignals(true);
        m_rail->setCurrentRow(best);
        m_rail->blockSignals(false);
    }
}

void ContextTab::setAllExpanded(bool expanded)
{
    for (auto& fn : m_expanders)
        fn(expanded);
}
