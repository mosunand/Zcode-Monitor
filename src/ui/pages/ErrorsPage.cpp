// ErrorsPage.cpp — see ErrorsPage.h.

#include "ErrorsPage.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <limits>

#include "ui/Theme.h"
#include "ui/UiUtil.h"
#include "core/DbService.h"
#include "core/LogTailService.h"
#include "util/Async.h"
#include "util/Format.h"

using namespace UiUtil;

// ── waterfall ────────────────────────────────────────────────

void WaterfallWidget::setEvents(const QVector<types::LogEvent>& events)
{
    m_events = events;
    if (events.isEmpty()) {
        m_minTs = 0;
        m_spanMs = 1;
    } else {
        qint64 minT = std::numeric_limits<qint64>::max();
        qint64 maxT = 0;
        for (const types::LogEvent& e : events) {
            if (!e.tsOk)
                continue;
            minT = qMin(minT, e.timestampMs);
            maxT = qMax(maxT, e.timestampMs);
        }
        m_minTs = minT;
        m_spanMs = qMax<qint64>(1, maxT - minT);
    }
    setFixedHeight(qMax(60, int(events.size()) * kRowH));
    update();
}

QSize WaterfallWidget::minimumSizeHint() const
{
    return QSize(400, qMax(60, int(m_events.size()) * kRowH));
}

void WaterfallWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const Palette& pal = Theme::instance().pal();
    QFont mono(QStringLiteral("Consolas"), 8);
    p.setFont(mono);

    for (int i = 0; i < m_events.size(); ++i) {
        const types::LogEvent& ev = m_events.at(i);
        const int y = i * kRowH;
        const QRect rowRect(0, y, width(), kRowH);

        // time (HH:mm:ss, first 8 chars like the web slice(0,8))
        p.setPen(pal.fg4);
        const QString timeText = ev.tsOk ? Format::fmtTime(ev.timestampMs).left(8)
                                          : QStringLiteral("—");
        p.drawText(QRect(0, y, 64, kRowH), Qt::AlignLeft | Qt::AlignVCenter, timeText);

        // label: dot + event + module
        const QString cat = ev.event.startsWith(QLatin1String("tool"))
            ? QStringLiteral("tool")
            : ev.event.startsWith(QLatin1String("model")) ? QStringLiteral("llm")
                                                          : QStringLiteral("other");
        const QColor color = cat == QLatin1String("tool")
            ? pal.catTool
            : (cat == QLatin1String("llm") ? pal.catLlm2 : pal.fg5);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPoint(74, y + kRowH / 2), 3, 3);
        p.setPen(pal.fg1);
        const QString labelText =
            ev.event.isEmpty() ? QStringLiteral("?") : ev.event;
        p.drawText(QRect(84, y, 300, kRowH), Qt::AlignLeft | Qt::AlignVCenter, labelText);
        p.setPen(pal.fg4);
        p.drawText(QRect(84, y, width() - 84 - 170, kRowH),
                   Qt::AlignRight | Qt::AlignVCenter, ev.module);

        // bar: left = min(offset/span*100, 60)% ; width = min(max(2, dur/span*100), 40)px
        const qint64 off = (ev.tsOk ? ev.timestampMs : m_minTs) - m_minTs;
        const double leftPct = qMin(double(off) / double(m_spanMs) * 100.0, 60.0);
        const double wRaw = (ev.durationOk ? ev.durationMs : 10) / double(m_spanMs) * 100.0;
        const int barW = qMin<qreal>(qMax<qreal>(2, wRaw), 40);
        const int barX = int(double(width() - 170) * leftPct / 100.0) + 84;
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRect(barX, y + kRowH / 2 - 4, barW, 8), 2, 2);

        // duration
        p.setPen(pal.fg3);
        p.drawText(QRect(width() - 80, y, 80, kRowH), Qt::AlignRight | Qt::AlignVCenter,
                   ev.durationOk ? Format::fmtMs(double(ev.durationMs)) : QStringLiteral("·"));
    }
}

// ── page ────────────────────────────────────────────────────

ErrorsPage::ErrorsPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    // inline styles bake in palette colors — reload on theme flips
    connect(&Theme::instance(), &Theme::changed, this, [this](bool) {
        if (m_loaded)
            load();
    });
}

QTableWidget* ErrorsPage::makeTable(const QStringList& headers)
{
    auto* t = new RowHoverTable;
    t->setColumnCount(int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    return t;
}

void ErrorsPage::buildUi()
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

    // toolbar
    auto* toolbar = new QWidget;
    auto* tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(0, 0, 0, 12);
    tb->setSpacing(10);
    auto* h1 = new QLabel(QStringLiteral("错误与链路排查"));
    h1->setObjectName(QStringLiteral("h1"));
    tb->addWidget(h1);
    tb->addStretch(1);
    m_windowCombo = new QComboBox;
    m_windowCombo->addItem(QStringLiteral("今天"), QStringLiteral("today"));
    m_windowCombo->addItem(QStringLiteral("近 24h"), QStringLiteral("24h"));
    m_windowCombo->addItem(QStringLiteral("近 7d"), QStringLiteral("7d"));
    m_windowCombo->addItem(QStringLiteral("全部"), QStringLiteral("all"));
    m_windowCombo->setCurrentIndex(1);
    connect(m_windowCombo, &QComboBox::currentIndexChanged, this, &ErrorsPage::load);
    tb->addWidget(m_windowCombo);
    m_refreshBtn = new QPushButton(QStringLiteral("↻"));
    m_refreshBtn->setProperty("cls", "ghost");
    connect(m_refreshBtn, &QPushButton::clicked, this, &ErrorsPage::load);
    tb->addWidget(m_refreshBtn);
    lay->addWidget(toolbar);

    // ── error summary (3 cards) ──
    lay->addWidget(sectionLabel(QStringLiteral("错误汇总"), QStringLiteral("按类型 / 工具")));
    auto* grid = new QWidget;
    auto* gl = new QGridLayout(grid);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(12);
    const auto summaryCard = [&](const QString& title, QTableWidget*& out) {
        auto* card = new CardFrame;
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(14, 12, 14, 12);
        auto* t = new QLabel(title);
        t->setObjectName(QStringLiteral("cardTitle"));
        cl->addWidget(t);
        out = makeTable({QStringLiteral("类型"), QStringLiteral("次数")});
        cl->addWidget(out);
        return card;
    };
    gl->addWidget(summaryCard(QStringLiteral("按模型错误类型"), m_summaryA), 0, 0);
    gl->addWidget(summaryCard(QStringLiteral("按工具"), m_summaryB), 0, 1);
    gl->addWidget(summaryCard(QStringLiteral("按工具错误类型"), m_summaryC), 0, 2);
    lay->addWidget(grid);

    // ── failed calls (2 cards) ──
    lay->addWidget(sectionLabel(QStringLiteral("失败调用"),
                                 QStringLiteral("点击行 → 带入 trace 还原")));
    auto* grid2 = new QWidget;
    auto* g2 = new QGridLayout(grid2);
    g2->setContentsMargins(0, 0, 0, 0);
    g2->setSpacing(12);
    auto* fmCard = new CardFrame;
    auto* fmcl = new QVBoxLayout(fmCard);
    fmcl->setContentsMargins(14, 10, 14, 12);
    auto* fmt1 = new QLabel(QStringLiteral("模型失败"));
    fmt1->setObjectName(QStringLiteral("cardTitle"));
    fmcl->addWidget(fmt1);
    m_failedModel = makeTable({QStringLiteral("时间"), QStringLiteral("会话"),
                               QStringLiteral("状态"), QStringLiteral("来源"),
                               QStringLiteral("模型"), QStringLiteral("时延"),
                               QStringLiteral("错误")});
    // session column → open session timeline; any other column → load its trace
    connect(m_failedModel, &QTableWidget::cellClicked, this,
            [this](int row, int col) {
                if (col == 1) {
                    if (auto* sid = m_failedModel->item(row, 1); sid)
                        emit openSessionRequested(sid->data(Qt::UserRole).toString(),
                                                  QStringLiteral("timeline"));
                } else if (auto* it = m_failedModel->item(row, 0); it) {
                    const QString traceId = it->data(Qt::UserRole + 1).toString();
                    if (!traceId.isEmpty()) {
                        m_traceInput->setText(traceId);
                        runTrace();
                    }
                }
            });
    fmcl->addWidget(m_failedModel);
    g2->addWidget(fmCard, 0, 0);
    auto* ftCard = new CardFrame;
    auto* ftcl = new QVBoxLayout(ftCard);
    ftcl->setContentsMargins(14, 10, 14, 12);
    auto* ftt1 = new QLabel(QStringLiteral("工具失败"));
    ftt1->setObjectName(QStringLiteral("cardTitle"));
    ftcl->addWidget(ftt1);
    m_failedTool = makeTable({QStringLiteral("时间"), QStringLiteral("会话"),
                              QStringLiteral("工具"), QStringLiteral("状态"),
                              QStringLiteral("exit"), QStringLiteral("时延"),
                              QStringLiteral("错误")});
    connect(m_failedTool, &QTableWidget::cellClicked, this,
            [this](int row, int col) {
                if (col == 1) {
                    if (auto* sid = m_failedTool->item(row, 1); sid)
                        emit openSessionRequested(sid->data(Qt::UserRole).toString(),
                                                  QStringLiteral("timeline"));
                } else if (auto* it = m_failedTool->item(row, 0); it) {
                    const QString traceId = it->data(Qt::UserRole + 1).toString();
                    if (!traceId.isEmpty()) {
                        m_traceInput->setText(traceId);
                        runTrace();
                    }
                }
            });
    ftcl->addWidget(m_failedTool);
    g2->addWidget(ftCard, 0, 1);
    lay->addWidget(grid2);

    // ── slow tools ──
    lay->addWidget(sectionLabel(QStringLiteral("最慢工具调用 Top 30")));
    auto* slowCard = new CardFrame;
    auto* scl = new QVBoxLayout(slowCard);
    scl->setContentsMargins(14, 10, 14, 12);
    m_slowTable = makeTable({QStringLiteral("时间"), QStringLiteral("会话"),
                              QStringLiteral("工具"), QStringLiteral("状态"),
                              QStringLiteral("时延"), QStringLiteral("错误")});
    scl->addWidget(m_slowTable);
    lay->addWidget(slowCard);

    // ── trace waterfall ──
    lay->addWidget(sectionLabel(QStringLiteral("Trace 链路还原"),
                                 QStringLiteral("输入 trace_id 还原事件瀑布")));
    auto* trCard = new CardFrame;
    auto* trl = new QVBoxLayout(trCard);
    trl->setContentsMargins(14, 12, 14, 14);
    trl->setSpacing(8);
    auto* inputRow = new QWidget;
    auto* irl = new QHBoxLayout(inputRow);
    irl->setContentsMargins(0, 0, 0, 0);
    irl->setSpacing(8);
    m_traceInput = new QLineEdit;
    m_traceInput->setPlaceholderText(
        QStringLiteral("trace_id (从上方失败记录点击带入，或粘贴)"));
    connect(m_traceInput, &QLineEdit::returnPressed, this, &ErrorsPage::runTrace);
    irl->addWidget(m_traceInput, 1);
    m_traceGo = new QPushButton(QStringLiteral("还原"));
    connect(m_traceGo, &QPushButton::clicked, this, &ErrorsPage::runTrace);
    irl->addWidget(m_traceGo);
    trl->addWidget(inputRow);

    m_traceOut = new QLabel(QStringLiteral("输入 trace_id 后点击「还原」"));
    m_traceOut->setProperty("cls", "faint");
    m_traceOut->setStyleSheet("font-size:8.5pt;");
    trl->addWidget(m_traceOut);

    m_traceResult = new QWidget;
    m_traceLay = new QVBoxLayout(m_traceResult);
    m_traceLay->setContentsMargins(0, 0, 0, 0);
    m_traceLay->setSpacing(8);
    auto* wfScroll = new QScrollArea;
    wfScroll->setWidgetResizable(true);
    wfScroll->setFrameShape(QFrame::StyledPanel);
    m_waterfall = new WaterfallWidget;
    wfScroll->setWidget(m_waterfall);
    wfScroll->setMaximumHeight(360);
    m_traceLay->addWidget(wfScroll);

    m_spanToggle = new QPushButton(QStringLiteral("展开原始 span 树 (JSON)"));
    m_spanToggle->setProperty("cls", "ghost");
    m_spanToggle->setCheckable(true);
    m_spanJson = new QPlainTextEdit;
    m_spanJson->setReadOnly(true);
    m_spanJson->setFont(QFont(QStringLiteral("Consolas"), 8));
    m_spanJson->setMaximumHeight(300);
    m_spanJson->setVisible(false);
    connect(m_spanToggle, &QPushButton::toggled, m_spanJson, &QPlainTextEdit::setVisible);
    connect(m_spanToggle, &QPushButton::toggled, this, [this](bool on) {
        m_spanToggle->setText(on ? QStringLiteral("收起原始 span 树 (JSON)")
                                  : QStringLiteral("展开原始 span 树 (JSON)"));
    });
    m_traceLay->addWidget(m_spanToggle);
    m_traceLay->addWidget(m_spanJson);
    m_traceResult->setVisible(false);
    trl->addWidget(m_traceResult);
    lay->addWidget(trCard);
    lay->addStretch(1);
}

void ErrorsPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (!m_loaded)
        load();
}

void ErrorsPage::load()
{
    const QString w = m_windowCombo->currentData().toString();

    // three aggregate queries + optional log scan — off the GUI thread
    const int seq = ++m_loadSeq;
    struct ErrData {
        types::ErrorSummary summary;
        types::FailedItems failed;
        QVector<types::SlowToolRow> slow;
    };
    Async::run<ErrData>(
        this,
        [w]() {
            auto& db = DbService::instance();
            qint64 sinceMs = -1;
            if (w == QLatin1String("today"))
                sinceMs = db.startOfDayMs();
            else if (w == QLatin1String("24h"))
                sinceMs = QDateTime::currentMSecsSinceEpoch() - 24LL * 3600000;
            else if (w == QLatin1String("7d"))
                sinceMs = QDateTime::currentMSecsSinceEpoch() - 7LL * 86400000;
            ErrData d;
            d.summary = db.errorSummary(sinceMs);
            d.failed = db.errorsList(sinceMs, QStringLiteral("both"), 200);
            d.slow = db.slowTools(sinceMs, 30);
            return d;
        },
        [this, seq](const ErrData& d) {
            if (seq != m_loadSeq)
                return; // a newer window/refresh superseded this one
            renderSummary(d.summary);
            renderFailed(d.failed);
            renderSlow(d.slow);
            m_loaded = true;
        });
}

void ErrorsPage::renderSummary(const types::ErrorSummary& s)
{
    const Palette& pal = Theme::instance().pal();
    const auto fill = [&](QTableWidget* t, const QVector<types::KVCount>& rows) {
        t->setRowCount(int(rows.size()));
        for (int i = 0; i < rows.size(); ++i) {
            auto* k = new QTableWidgetItem(rows[i].k);
            k->setFlags(Qt::ItemIsEnabled);
            k->setFont(QFont(QStringLiteral("Consolas"), 9));
            t->setItem(i, 0, k);
            auto* n = new QTableWidgetItem(Format::fmtInt64(rows[i].n));
            n->setFlags(Qt::ItemIsEnabled);
            n->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            t->setItem(i, 1, n);
        }
        if (rows.isEmpty()) {
            t->setRowCount(1);
            auto* empty = new QTableWidgetItem(QStringLiteral("无"));
            empty->setFlags(Qt::ItemIsEnabled);
            empty->setForeground(pal.fg4);
            t->setItem(0, 0, empty);
        }
        t->resizeColumnsToContents();
    };
    fill(m_summaryA, s.byModelErrorType);
    fill(m_summaryB, s.byToolName);
    fill(m_summaryC, s.byToolErrorType);
    for (QTableWidget* t : {m_summaryA, m_summaryB, m_summaryC})
        t->setFixedHeight(UiUtil::tableContentHeight(t, 260));
}

void ErrorsPage::fillSessionLinkRow(QTableWidget* table, int row, int col,
                                    const QString& sessionId)
{
    auto* it = new QTableWidgetItem(sessionId.left(8));
    it->setFlags(Qt::ItemIsEnabled);
    it->setForeground(Theme::instance().pal().accent);
    it->setToolTip(sessionId);
    it->setData(Qt::UserRole, sessionId);
    table->setItem(row, col, it);
}

void ErrorsPage::renderFailed(const types::FailedItems& items)
{
    const Palette& pal = Theme::instance().pal();
    // clear spans from a previous empty state, or the first data row gets swallowed
    m_failedModel->clearSpans();
    m_failedTool->clearSpans();
    m_failedModel->setRowCount(int(items.model.size()));
    for (int i = 0; i < items.model.size(); ++i) {
        const types::FailedModelRow& m = items.model[i];
        auto* time = new QTableWidgetItem(Format::fmtTime(m.startedMs));
        time->setFlags(Qt::ItemIsEnabled);
        time->setFont(QFont(QStringLiteral("Consolas"), 8));
        time->setForeground(pal.fg4);
        time->setData(Qt::UserRole + 1, m.traceId); // row click → trace restore
        m_failedModel->setItem(i, 0, time);
        fillSessionLinkRow(m_failedModel, i, 1, m.sessionId);
        m_failedModel->setCellWidget(i, 2, new BadgeLabel(m.status, statusBadgeKind(m.status)));
        auto* src = new QTableWidgetItem(m.querySource);
        src->setFlags(Qt::ItemIsEnabled);
        src->setForeground(pal.fg3);
        m_failedModel->setItem(i, 3, src);
        auto* model = new QTableWidgetItem(m.modelId.isEmpty()
            ? QStringLiteral("?") : m.modelId);
        model->setFlags(Qt::ItemIsEnabled);
        model->setFont(QFont(QStringLiteral("Consolas"), 8));
        m_failedModel->setItem(i, 4, model);
        auto* dur = new QTableWidgetItem(m.durationOk
            ? Format::fmtMs(double(m.durationMs)) : QStringLiteral("—"));
        dur->setFlags(Qt::ItemIsEnabled);
        dur->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_failedModel->setItem(i, 5, dur);
        QString err = m.errorMessage.isEmpty() ? m.errorType : m.errorMessage;
        if (err.size() > 120)
            err = err.left(120);
        auto* errIt = new QTableWidgetItem(err);
        errIt->setFlags(Qt::ItemIsEnabled);
        errIt->setForeground(pal.sevErr);
        errIt->setFont(QFont(QStringLiteral("Consolas"), 8));
        m_failedModel->setItem(i, 6, errIt);
    }

    m_failedTool->setRowCount(int(items.tool.size()));
    for (int i = 0; i < items.tool.size(); ++i) {
        const types::FailedToolRow& t = items.tool[i];
        auto* time = new QTableWidgetItem(Format::fmtTime(t.startedMs));
        time->setFlags(Qt::ItemIsEnabled);
        time->setFont(QFont(QStringLiteral("Consolas"), 8));
        time->setForeground(pal.fg4);
        time->setData(Qt::UserRole + 1, t.traceId); // row click → trace restore
        m_failedTool->setItem(i, 0, time);
        fillSessionLinkRow(m_failedTool, i, 1, t.sessionId);
        auto* tool = new QTableWidgetItem(t.toolName);
        tool->setFlags(Qt::ItemIsEnabled);
        tool->setFont(QFont(QStringLiteral("Consolas"), 8));
        m_failedTool->setItem(i, 2, tool);
        m_failedTool->setCellWidget(i, 3,
                                    new BadgeLabel(t.status, statusBadgeKind(t.status)));
        auto* exitIt = new QTableWidgetItem(t.hasExit ? QString::number(t.exitCode)
                                                      : QStringLiteral("—"));
        exitIt->setFlags(Qt::ItemIsEnabled);
        exitIt->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_failedTool->setItem(i, 4, exitIt);
        auto* dur = new QTableWidgetItem(t.durationOk
            ? Format::fmtMs(double(t.durationMs)) : QStringLiteral("—"));
        dur->setFlags(Qt::ItemIsEnabled);
        dur->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_failedTool->setItem(i, 5, dur);
        QString err = t.errorMessage.isEmpty() ? t.errorType : t.errorMessage;
        if (err.size() > 120)
            err = err.left(120);
        auto* errIt = new QTableWidgetItem(err);
        errIt->setFlags(Qt::ItemIsEnabled);
        errIt->setForeground(pal.sevErr);
        errIt->setFont(QFont(QStringLiteral("Consolas"), 8));
        m_failedTool->setItem(i, 6, errIt);
    }

    m_failedModel->setFixedHeight(0); // recalculated after fill below
    if (items.model.isEmpty()) {
        m_failedModel->insertRow(0);
        auto* e = new QTableWidgetItem(QStringLiteral("无失败"));
        e->setFlags(Qt::ItemIsEnabled);
        e->setForeground(pal.fg4);
        e->setTextAlignment(Qt::AlignCenter);
        m_failedModel->setItem(0, 0, e);
        m_failedModel->setSpan(0, 0, 1, 7);
    }
    if (items.tool.isEmpty()) {
        m_failedTool->insertRow(0);
        auto* e = new QTableWidgetItem(QStringLiteral("无失败"));
        e->setFlags(Qt::ItemIsEnabled);
        e->setForeground(pal.fg4);
        e->setTextAlignment(Qt::AlignCenter);
        m_failedTool->setItem(0, 0, e);
        m_failedTool->setSpan(0, 0, 1, 7);
    }
    m_failedModel->resizeColumnsToContents();
    m_failedTool->resizeColumnsToContents();
    m_failedModel->setFixedHeight(UiUtil::tableContentHeight(m_failedModel, 520));
    m_failedTool->setFixedHeight(UiUtil::tableContentHeight(m_failedTool, 520));
}

void ErrorsPage::renderSlow(const QVector<types::SlowToolRow>& items)
{
    const Palette& pal = Theme::instance().pal();
    m_slowTable->clearSpans(); // stale spans from a previous empty state
    m_slowTable->setRowCount(int(items.size()));
    for (int i = 0; i < items.size(); ++i) {
        const types::SlowToolRow& t = items[i];
        auto* time = new QTableWidgetItem(Format::fmtTime(t.startedMs));
        time->setFlags(Qt::ItemIsEnabled);
        time->setFont(QFont(QStringLiteral("Consolas"), 8));
        time->setForeground(pal.fg4);
        m_slowTable->setItem(i, 0, time);
        fillSessionLinkRow(m_slowTable, i, 1, t.sessionId);
        auto* tool = new QTableWidgetItem(t.toolName);
        tool->setFlags(Qt::ItemIsEnabled);
        tool->setFont(QFont(QStringLiteral("Consolas"), 8));
        m_slowTable->setItem(i, 2, tool);
        m_slowTable->setCellWidget(i, 3,
                                   new BadgeLabel(t.status, statusBadgeKind(t.status)));
        auto* dur = new QTableWidgetItem(t.durationOk
            ? Format::fmtMs(double(t.durationMs)) : QStringLiteral("—"));
        dur->setFlags(Qt::ItemIsEnabled);
        dur->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_slowTable->setItem(i, 4, dur);
        QString err = t.err;
        if (err.size() > 80)
            err = err.left(80);
        auto* errIt = new QTableWidgetItem(err);
        errIt->setFlags(Qt::ItemIsEnabled);
        errIt->setForeground(pal.fg4);
        errIt->setFont(QFont(QStringLiteral("Consolas"), 8));
        m_slowTable->setItem(i, 5, errIt);
    }
    if (items.isEmpty()) {
        m_slowTable->insertRow(0);
        auto* e = new QTableWidgetItem(QStringLiteral("无数据"));
        e->setFlags(Qt::ItemIsEnabled);
        e->setForeground(Theme::instance().pal().fg4);
        e->setTextAlignment(Qt::AlignCenter);
        m_slowTable->setItem(0, 0, e);
        m_slowTable->setSpan(0, 0, 1, 6);
    }
    m_slowTable->resizeColumnsToContents();
    m_slowTable->setFixedHeight(UiUtil::tableContentHeight(m_slowTable, 520));
}

void ErrorsPage::runTrace()
{
    const QString traceId = m_traceInput->text().trimmed();
    if (traceId.isEmpty()) {
        m_traceOut->setText(QStringLiteral("请输入 trace_id"));
        m_traceResult->setVisible(false);
        return;
    }
    m_traceOut->setText(QStringLiteral("解析日志…"));
    // today+yesterday log scan can read MBs — off the GUI thread
    const int seq = ++m_traceSeq;
    struct TraceData {
        QVector<types::LogEvent> events;
        QVector<types::SpanNode> forest;
    };
    Async::run<TraceData>(
        this,
        [traceId]() {
            TraceData d;
            d.events = LogTailService::eventsForTrace(traceId);
            d.forest = LogTailService::buildSpanForest(d.events);
            return d;
        },
        [this, seq](const TraceData& d) {
            if (seq != m_traceSeq)
                return; // a newer trace request superseded this one
            if (d.events.isEmpty()) {
                m_traceOut->setText(
                    QStringLiteral("日志中无此 trace_id %1 的事件（日志按 UTC 天滚动，可能已被清理）")
                        .arg(m_traceInput->text().trimmed()));
                m_traceResult->setVisible(false);
                return;
            }
            const QVector<types::LogEvent>& events = d.events;
            // count actual spans (deduplicated by spanId), not raw events
            std::function<int(const QVector<types::SpanNode>&)> countSpans =
                [&](const QVector<types::SpanNode>& nodes) -> int {
                int n = int(nodes.size());
                for (const types::SpanNode& x : nodes)
                    n += countSpans(x.children);
                return n;
            };
            m_traceOut->setText(QStringLiteral("%1 个事件 · 跨度 %2 · %3 条 span 记录")
                                    .arg(events.size())
                                    .arg(Format::fmtMs(double((events.last().tsOk ? events.last().timestampMs : 0)
                                                             - (events.first().tsOk ? events.first().timestampMs : 0))))
                                    .arg(countSpans(d.forest)));
            m_waterfall->setEvents(events);

            // raw span forest JSON
            std::function<void(const QVector<types::SpanNode>&, QJsonArray&)> toArray =
                [&](const QVector<types::SpanNode>& nodes, QJsonArray& arr) {
                for (const types::SpanNode& n : nodes) {
                    QJsonObject o = n.event.raw;
                    if (!n.children.isEmpty()) {
                        QJsonArray kids;
                        toArray(n.children, kids);
                        o.insert(QStringLiteral("children"), kids);
                    }
                    arr.append(o);
                }
            };
            QJsonArray arr;
            toArray(d.forest, arr);
            m_spanJson->setPlainText(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Indented)));
            m_traceResult->setVisible(true);
        });
}
