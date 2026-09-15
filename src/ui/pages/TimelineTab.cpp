// TimelineTab.cpp — see TimelineTab.h.

#include "TimelineTab.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/Theme.h"
#include "ui/UiUtil.h"
#include "core/TranscriptService.h"
#include "util/Async.h"
#include "util/Format.h"

using namespace UiUtil;

namespace {

// web FILTERS in timeline.js
struct FilterDef {
    const char* id;
    const char* label;
    const char* cats; // comma list or empty
    bool errorsOnly = false;
};
const QVector<FilterDef> FILTERS = {
    {"all", "全部", "", false},
    {"prompt", "prompt", "prompt", false},
    {"llm", "llm→", "llm", false},
    {"tool", "tools", "tool", false},
    {"network", "network", "network", false},
    {"usage", "usage", "usage", false},
    {"errors", "⚠ 错误", "", true},
};

// label key color (web .desc .k.<cat>)
QColor labelColor(const QString& category)
{
    const Palette& pal = Theme::instance().pal();
    if (category == QLatin1String("llm"))
        return pal.catLlm2;
    if (category == QLatin1String("tool"))
        return pal.catTool;
    if (category == QLatin1String("network"))
        return pal.catNetwork;
    if (category == QLatin1String("prompt"))
        return pal.catPrompt;
    if (category == QLatin1String("usage"))
        return pal.catUsage;
    if (category == QLatin1String("lifecycle"))
        return pal.catLifecycle;
    return pal.fg1;
}

bool isError(const types::TranscriptEvent& ev)
{
    if (ev.type == QLatin1String("turn_complete")) {
        const QString rt = ev.payload.value(QLatin1String("resultType")).toString();
        if (!rt.isEmpty() && rt != QLatin1String("success"))
            return true;
    }
    const QString s = ev.payload.value(QLatin1String("status")).toString()
                    + ev.payload.value(QLatin1String("resultType")).toString();
    return s.contains(QLatin1String("error"), Qt::CaseInsensitive)
        || s.contains(QLatin1String("fail"), Qt::CaseInsensitive);
}

// coalesceStreaming: collapse runs of model_streaming text/reasoning deltas
QVector<types::TranscriptEvent> coalesceStreaming(const QVector<types::TranscriptEvent>& events)
{
    QVector<types::TranscriptEvent> out;
    struct Buf {
        int kind = 0; // 1=text, 2=reasoning
        int chars = 0;
        int count = 0;
        int firstIdx = -1;
    } buf;
    const auto flush = [&] {
        if (buf.kind == 0)
            return;
        const types::TranscriptEvent& ref = events.at(buf.firstIdx);
        if (buf.kind == 2) {
            types::TranscriptEvent e = ref;
            e.type = QStringLiteral("reasoning_coalesced");
            e.category = QStringLiteral("llm");
            e.label = QStringLiteral("think");
            e.icon = QStringLiteral("◆");
            e.summary = QStringLiteral("推理输出 +%1 字符 (已折叠 %2 段，点开看原文)")
                            .arg(Format::fmtNum(double(buf.chars)))
                            .arg(buf.count);
            e.payload = QJsonObject{{"coalesced", true},
                                      {"chars", double(buf.chars)},
                                      {"segments", double(buf.count)}};
            out.push_back(e);
        } else if (buf.kind == 1) {
            types::TranscriptEvent e = ref;
            e.type = QStringLiteral("text_coalesced");
            e.category = QStringLiteral("llm");
            e.label = QStringLiteral("text");
            e.icon = QStringLiteral("✎");
            e.summary = QStringLiteral("文本输出 +%1 字符 (%2 段)")
                            .arg(Format::fmtNum(double(buf.chars)))
                            .arg(buf.count);
            e.payload = QJsonObject{{"coalesced", true},
                                      {"chars", double(buf.chars)},
                                      {"segments", double(buf.count)}};
            out.push_back(e);
        } else {
            out.push_back(events.at(buf.firstIdx));
        }
        buf = Buf{};
    };
    for (int i = 0; i < events.size(); ++i) {
        const types::TranscriptEvent& ev = events.at(i);
        if (ev.type == QLatin1String("model_streaming")) {
            const QString k = ev.payload.value(QLatin1String("kind")).toString();
            if (k == QLatin1String("text_delta") || k == QLatin1String("reasoning_delta")) {
                const int len = ev.payload.value(QLatin1String("delta")).toString().size();
                const int kind = k == QLatin1String("text_delta") ? 1 : 2;
                if (buf.kind == kind) {
                    buf.chars += len;
                    buf.count++;
                    continue;
                }
                flush();
                buf.kind = kind;
                buf.chars = len;
                buf.count = 1;
                buf.firstIdx = i;
                continue;
            }
            flush();
            out.push_back(ev);
            continue;
        }
        flush();
        out.push_back(ev);
    }
    flush();
    return out;
}

// the wire-style event row painter
class EventDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        const bool isTop = !index.parent().isValid();
        if (!isTop) {
            // payload child item: plain monospace text
            QStyleOptionViewItem o = option;
            p->fillRect(option.rect, Theme::instance().pal().bg);
            p->setPen(Theme::instance().pal().fg2);
            QFont f = p->font();
            f.setFamily(QStringLiteral("Consolas"));
            f.setPointSizeF(7.5);
            p->setFont(f);
            const QString text = index.data(Qt::DisplayRole).toString();
            p->drawText(option.rect.adjusted(12, 6, -8, -6),
                        Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, text);
            return;
        }
        const QModelIndex& i = index;
        const QVariant vSeq = i.data(Qt::UserRole);
        const QVariant vTime = i.data(Qt::UserRole + 1);
        const QVariant vCat = i.data(Qt::UserRole + 2);
        const QVariant vLabel = i.data(Qt::UserRole + 3);
        const QVariant vSummary = i.data(Qt::UserRole + 4);
        const QVariant vIcon = i.data(Qt::UserRole + 5);
        const QVariant vErr = i.data(Qt::UserRole + 6);
        const QVariant vHasPayload = i.data(Qt::UserRole + 7);

        const Palette& pal = Theme::instance().pal();
        const QRect rect = option.rect;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        if (vErr.toBool()) {
            QColor tint = pal.sevErr;
            tint.setAlpha(18);
            p->fillRect(rect, tint);
        } else if (option.state & QStyle::State_MouseOver) {
            p->fillRect(rect, pal.surface1);
        }
        p->setPen(QPen(pal.borderSoft, 1));
        p->drawLine(rect.left(), rect.bottom(), rect.right(), rect.bottom());

        QFont mono(QStringLiteral("Consolas"));
        QFont seqF = mono;
        seqF.setPointSizeF(7.5);
        p->setFont(seqF);
        p->setPen(pal.fg5);
        const QString seqText = vSeq.isValid() ? vSeq.toString() : QStringLiteral("·");
        p->drawText(QRect(rect.left() + 8, rect.y(), 34, rect.height()),
                    Qt::AlignRight | Qt::AlignVCenter, seqText);

        p->setPen(pal.fg4);
        p->drawText(QRect(rect.left() + 48, rect.y(), 64, rect.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, vTime.toString());

        const QString cat = vCat.toString();
        p->setPen(Qt::NoPen);
        p->setBrush(catColor(cat));
        p->drawEllipse(QPoint(rect.left() + 122, rect.center().y()), 4, 4);

        // right: payload indicator
        if (vHasPayload.toBool()) {
            QFont rf = mono;
            rf.setPointSizeF(7.5);
            p->setFont(rf);
            p->setPen(pal.fg4);
            p->drawText(QRect(rect.right() - 90, rect.y(), 84, rect.height()),
                        Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("payload ▾"));
        }

        // desc: "icon label" colored + summary
        QFont labelF = mono;
        labelF.setPointSizeF(8.5);
        labelF.setBold(true);
        QFontMetrics lfm(labelF);
        const QString labelText = (vIcon.toString() + QLatin1Char(' ') + vLabel.toString()).trimmed();
        int x = rect.left() + 136;
        p->setFont(labelF);
        p->setPen(labelColor(cat));
        p->drawText(QRect(x, rect.y(), lfm.horizontalAdvance(labelText) + 2, rect.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, labelText);
        x += lfm.horizontalAdvance(labelText) + 7;

        QFont bodyF = mono;
        bodyF.setPointSizeF(8.5);
        QFontMetrics bfm(bodyF);
        const int avail = rect.right() - 100 - x;
        const QString summary =
            bfm.elidedText(vSummary.toString(), Qt::ElideRight, qMax(30, avail));
        p->setFont(bodyF);
        p->setPen(pal.fg1);
        p->drawText(QRect(x, rect.y(), bfm.horizontalAdvance(summary) + 4, rect.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, summary);
        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        if (!index.parent().isValid())
            return QSize(300, 28);
        // payload item height depends on text length; approximate via the text
        const QString text = index.data(Qt::DisplayRole).toString();
        QFont f(QStringLiteral("Consolas"), 8);
        QFontMetrics fm(f);
        const int lines = text.count(QLatin1Char('\n')) + 1;
        const int widthChars = 110;
        int estLines = 0;
        for (const QString& line : text.split(QLatin1Char('\n')))
            estLines += qMax(1, (line.size() + widthChars - 1) / widthChars);
        Q_UNUSED(lines);
        return QSize(300, qMin(2000, estLines * fm.height() + 12));
    }
};

} // namespace

TimelineTab::TimelineTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    // theme flips only need a re-render — keep the active filter and data;
    // the not-found card bakes colors too, so re-render that branch as well
    connect(&Theme::instance(), &Theme::changed, this, [this](bool) {
        if (m_sessionId.isEmpty())
            return;
        loadData(); // renders the not-found card when !found
    });
}

void TimelineTab::buildUi()
{
    m_body = new QVBoxLayout(this);
    m_body->setContentsMargins(16, 14, 16, 16);
    m_body->setSpacing(0);
}

void TimelineTab::load(const QString& sessionId)
{
    m_sessionId = sessionId;
    m_activeFilter = QStringLiteral("all");
    loadData();
}

void TimelineTab::loadData()
{
    // loading placeholder while the pool thread parses
    while (m_body->count()) {
        auto* item = m_body->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    auto* loading = new QLabel(QStringLiteral("加载事件流…"));
    loading->setAlignment(Qt::AlignCenter);
    loading->setProperty("cls", "faint");
    loading->setStyleSheet("padding:30px;");
    m_body->addWidget(loading);

    // transcript.jsonl can be MBs — parse it on a pool thread and apply on
    // the GUI thread. A stale request (session switched again meanwhile) is
    // dropped via the sequence number, so only the newest result renders.
    const QString sessionId = m_sessionId;
    const int seq = ++m_requestSeq;
    Async::run<types::TranscriptData>(
        this,
        [sessionId]() { return TranscriptService::readTranscript(sessionId, 4000); },
        [this, seq, sessionId](const types::TranscriptData& d) {
            if (seq != m_requestSeq || sessionId != m_sessionId)
                return; // superseded by a newer load
            applyData(d, sessionId);
        });
}

void TimelineTab::applyData(const types::TranscriptData& d, const QString& sessionId)
{
    m_data = d;
    if (!m_data.found) {
        renderNotFound(sessionId);
        return;
    }
    // rebuild UI
    while (m_body->count()) {
        auto* item = m_body->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    renderHeader();
    renderEvents();
}

void TimelineTab::renderNotFound(const QString& sessionId)
{
    while (m_body->count()) {
        auto* item = m_body->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    const Palette& pal = Theme::instance().pal();
    auto* card = new CardFrame;
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(14, 14, 14, 14);
    cl->setSpacing(8);
    auto* h = new QLabel(QStringLiteral("该会话没有 transcript.jsonl"));
    h->setStyleSheet(QStringLiteral("color:%1;font-size:10pt;font-weight:600;")
                         .arg(pal.fg1.name()));
    cl->addWidget(h);
    auto* p1 = new QLabel(QStringLiteral(
        "主交互会话（interactive）不产生 transcript 事件流——它的对话记录在 SQLite 的 "
        "message/part 表里，请切到 Context 标签查看完整对话（含推理思考）。"));
    p1->setWordWrap(true);
    p1->setStyleSheet(QStringLiteral("color:%1;font-size:9pt;").arg(pal.fg2.name()));
    cl->addWidget(p1);
    auto* p2 = new QLabel(QStringLiteral("只有子 agent（subagent）才有 transcript.jsonl 实时事件流。"));
    p2->setWordWrap(true);
    p2->setStyleSheet(QStringLiteral("color:%1;font-size:9pt;").arg(pal.fg2.name()));
    cl->addWidget(p2);
    auto* link = new QLabel(
        QStringLiteral("<a href='#' style='color:%1'>→ 去看 Context（完整对话 + reasoning）</a>")
            .arg(pal.accent.name()));
    link->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(link, &QLabel::linkActivated, this,
            [this, sessionId](const QString&) { emit openSessionRequested(sessionId, "context"); });
    cl->addWidget(link);
    m_body->addWidget(card);
    m_body->addStretch(1);
}

void TimelineTab::renderHeader()
{
    const Palette& pal = Theme::instance().pal();
    const types::TranscriptMeta& meta = m_data.meta;
    const types::TranscriptAggregate& agg = m_data.aggregate;

    m_headerCard = new CardFrame;
    auto* hl = new QVBoxLayout(m_headerCard);
    hl->setContentsMargins(14, 14, 14, 14);
    hl->setSpacing(8);

    auto* top = new QWidget;
    auto* tl = new QHBoxLayout(top);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(16);
    tl->addWidget(new BadgeLabel(
        meta.profileId.isEmpty() ? QStringLiteral("?") : meta.profileId,
        meta.profileId == QLatin1String("Explore") ? QStringLiteral("teal")
                                                   : QStringLiteral("purple")));
    auto* desc = new QLabel(meta.description);
    desc->setStyleSheet(QStringLiteral("color:%1;font-size:9pt;").arg(pal.fg3.name()));
    desc->setWordWrap(true);
    tl->addWidget(desc, 1);
    hl->addWidget(top);

    auto* kv = new QGridLayout;
    kv->setHorizontalSpacing(12);
    kv->setVerticalSpacing(3);
    kv->setColumnStretch(1, 1);
    const auto addKv = [&](const QString& k, QWidget* v) {
        auto* kl = new QLabel(k);
        kl->setStyleSheet(
            QStringLiteral("color:%1;font-size:8.5pt;").arg(pal.fg4.name()));
        v->setStyleSheet(
            QStringLiteral("color:%1;font-family:'Consolas';font-size:8.5pt;")
                .arg(pal.fg1.name()));
        const int r = kv->rowCount();
        kv->addWidget(kl, r, 0);
        kv->addWidget(v, r, 1);
    };
    addKv(QStringLiteral("状态"), new QLabel(meta.status.isEmpty()
                                                   ? QStringLiteral("?")
                                                   : meta.status));
    addKv(QStringLiteral("耗时"),
          new QLabel(meta.totalDurationOk ? Format::fmtMs(double(meta.totalDurationMs))
                                           : QStringLiteral("?")));
    addKv(QStringLiteral("总 token"),
          new QLabel(meta.totalTokensOk ? Format::fmtNum(double(meta.totalTokens))
                                         : QStringLiteral("?")));
    addKv(QStringLiteral("工具调用"),
          new QLabel(meta.totalToolUseOk
                          ? QString::number(meta.totalToolUseCount)
                          : QStringLiteral("?")));
    addKv(QStringLiteral("事件总数"), new QLabel(QString::number(m_data.count)));
    {
        auto* parentL = new QLabel(
            meta.parentSessionId.isEmpty()
                ? QStringLiteral("—")
                : QStringLiteral("<a href='#' style='color:%1'>%2…</a>")
                      .arg(pal.accent.name(), meta.parentSessionId.left(12)));
        if (!meta.parentSessionId.isEmpty()) {
            const QString pid = meta.parentSessionId;
            parentL->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
            connect(parentL, &QLabel::linkActivated, this,
                    [this, pid](const QString&) {
                        emit openSessionRequested(pid, QStringLiteral("timeline"));
                    });
        }
        addKv(QStringLiteral("parent"), parentL);
    }
    addKv(QStringLiteral("spawn by"),
          new QLabel(meta.parentToolUseId.isEmpty() ? QStringLiteral("—")
                                                      : meta.parentToolUseId));
    hl->addLayout(kv);

    // tool top 8 badges
    auto tools = agg.tools;
    std::sort(tools.begin(), tools.end(),
              [](const types::KVCount& a, const types::KVCount& b) { return a.n > b.n; });
    if (!tools.isEmpty()) {
        auto* badges = new QWidget;
        auto* bl = new QHBoxLayout(badges);
        bl->setContentsMargins(0, 6, 0, 0);
        bl->setSpacing(6);
        for (int i = 0; i < qMin(8, tools.size()); ++i) {
            bl->addWidget(new BadgeLabel(
                tools[i].k + QStringLiteral(" ") + QString::number(tools[i].n),
                QStringLiteral("teal")));
        }
        bl->addStretch(1);
        hl->addWidget(badges);
    }
    m_body->addWidget(m_headerCard);
    m_body->addSpacing(10);
}

void TimelineTab::renderEvents()
{
    const Palette& pal = Theme::instance().pal();

    // filter bar
    m_filterBar = new QWidget;
    auto* fl = new QHBoxLayout(m_filterBar);
    fl->setContentsMargins(0, 0, 0, 8);
    fl->setSpacing(6);
    for (const FilterDef& f : FILTERS) {
        auto* btn = new QPushButton(QString::fromUtf8(f.label));
        btn->setProperty("cls",
                         QString::fromLatin1(f.id) == m_activeFilter
                             ? QStringLiteral("on")
                             : QStringLiteral("ghost"));
        const QString fid = QString::fromLatin1(f.id);
        connect(btn, &QPushButton::clicked, this, [this, fid, btn] {
            setFilter(fid);
            // restyle all filter buttons
            const auto btns = btn->parentWidget()->findChildren<QPushButton*>();
            for (QPushButton* b : btns) {
                b->setProperty("cls", b == btn ? QStringLiteral("on")
                                                : QStringLiteral("ghost"));
                b->style()->unpolish(b);
                b->style()->polish(b);
            }
        });
        fl->addWidget(btn);
    }
    fl->addStretch(1);
    m_countLabel = new QLabel;
    m_countLabel->setStyleSheet(
        QStringLiteral("color:%1;font-size:8.5pt;").arg(pal.fg4.name()));
    fl->addWidget(m_countLabel);
    m_body->addWidget(m_filterBar);

    // events tree
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(1);
    m_tree->header()->hide();
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(false);
    m_tree->setItemDelegate(new EventDelegate(m_tree));
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setMaximumHeight(1000); // web: max-height 65vh + scroll
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item->parent())
            item->setExpanded(!item->isExpanded());
    });
    // payload rows hold raw JSON; format to text the first time they open
    connect(m_tree, &QTreeWidget::itemExpanded, this, [](QTreeWidgetItem* item) {
        if (!item->parent())
            return;
        const QVariant raw = item->data(0, Qt::UserRole);
        if (!raw.isNull() && !raw.toJsonObject().isEmpty()) {
            item->setText(0, QString::fromUtf8(
                QJsonDocument(raw.toJsonObject()).toJson(QJsonDocument::Indented)));
            item->setData(0, Qt::UserRole, QVariant()); // format only once
        }
    });
    m_body->addWidget(m_tree, 1);

    fillTree();
}

void TimelineTab::setFilter(const QString& filterId)
{
    m_activeFilter = filterId;
    fillTree();
}

void TimelineTab::fillTree()
{
    if (!m_tree)
        return;
    m_tree->clear();

    // apply category / error filter
    const FilterDef* def = nullptr;
    for (const FilterDef& f : FILTERS) {
        if (QString::fromLatin1(f.id) == m_activeFilter) {
            def = &f;
            break;
        }
    }
    QVector<types::TranscriptEvent> filtered = m_data.events;
    if (def && def->cats[0] != '\0') {
        const QStringList cats = QString::fromUtf8(def->cats).split(QLatin1Char(','));
        QVector<types::TranscriptEvent> out;
        for (const types::TranscriptEvent& e : m_data.events)
            if (cats.contains(e.category))
                out.push_back(e);
        filtered = out;
    } else if (def && def->errorsOnly) {
        QVector<types::TranscriptEvent> out;
        for (const types::TranscriptEvent& e : m_data.events)
            if (isError(e))
                out.push_back(e);
        filtered = out;
    }

    const QVector<types::TranscriptEvent> coalesced = coalesceStreaming(filtered);
    if (m_countLabel)
        m_countLabel->setText(QStringLiteral("显示 %1 / %2")
                                  .arg(coalesced.size())
                                  .arg(m_data.events.size()));

    m_tree->setUpdatesEnabled(false);
    for (const types::TranscriptEvent& ev : coalesced) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setData(0, Qt::UserRole,
                      ev.seqOk ? QVariant(QString::number(ev.sequenceNumber)) : QVariant());
        item->setData(0, Qt::UserRole + 1,
                      ev.tsOk ? Format::fmtTime(ev.timestampMs) : QStringLiteral("—"));
        item->setData(0, Qt::UserRole + 2, ev.category);
        item->setData(0, Qt::UserRole + 3, ev.label);
        item->setData(0, Qt::UserRole + 4, ev.summary);
        item->setData(0, Qt::UserRole + 5, ev.icon);
        item->setData(0, Qt::UserRole + 6, isError(ev));
        item->setSizeHint(0, QSize(100, 28));
        if (!ev.payload.isEmpty()) {
            item->setData(0, Qt::UserRole + 7, true);
            auto* payloadItem = new QTreeWidgetItem(item);
            // store the raw JSON; format to text only when expanded (lazy —
            // pre-formatting 4000 payloads dominated the original load time)
            payloadItem->setData(0, Qt::UserRole, ev.payload);
            payloadItem->setText(0, QStringLiteral(" ")); // placeholder height
        } else {
            item->setData(0, Qt::UserRole + 7, false);
        }
    }
    if (coalesced.isEmpty()) {
        auto* empty = new QTreeWidgetItem(m_tree);
        empty->setText(0, QStringLiteral("无匹配事件"));
        empty->setFlags(Qt::NoItemFlags);
    }
    m_tree->setUpdatesEnabled(true);
}
