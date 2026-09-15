// OverviewPage.cpp — see OverviewPage.h.

#include "OverviewPage.h"

#include <QComboBox>
#include <QGridLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "ui/widgets/LiveFeed.h"
#include "ui/Theme.h"
#include "ui/UiUtil.h"
#include "ui/widgets/KpiCard.h"
#include "ui/widgets/MiniChart.h"
#include "core/DbService.h"
#include "core/LivePoller.h"
#include "util/Async.h"
#include "util/Format.h"

using namespace UiUtil;

namespace {

QTableWidgetItem* numItem(const QString& text)
{
    auto* it = new QTableWidgetItem(text);
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    it->setFlags(Qt::ItemIsEnabled);
    return it;
}

QTableWidgetItem* txtItem(const QString& text)
{
    auto* it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled);
    return it;
}

// token-speed tier colors for table text (web .spd-red/.spd-yellow/.spd-green)
void tint(QTableWidgetItem* it, const QColor& c)
{
    it->setForeground(c);
}

QColor speedTierColor(double tps)
{
    return UiUtil::speedColor(Format::speedTier(tps));
}

// web shows "模型调用 (24h)" — map the window id to its short label
QString windowLabel(const QString& window)
{
    if (window == QLatin1String("today"))
        return QStringLiteral("today");
    if (window == QLatin1String("7d"))
        return QStringLiteral("7d");
    return QStringLiteral("24h");
}

} // namespace

OverviewPage::OverviewPage(QWidget* parent)
    : QWidget(parent)
{
    m_poller = new LivePoller(this);
    connect(m_poller, &LivePoller::modelRows, this, &OverviewPage::onModelRows);
    connect(m_poller, &LivePoller::toolRows, this, &OverviewPage::onToolRows);
    connect(m_poller, &LivePoller::pollError, this, &OverviewPage::onLiveError);
    connect(m_poller, &LivePoller::recovered, this, &OverviewPage::onLiveRecovered);

    connect(&Theme::instance(), &Theme::changed, this, [this](bool) { renderData(); });

    buildUi();
}

void OverviewPage::buildUi()
{
    auto* rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(0, 0, 0, 0);

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    content->setMaximumWidth(1400);
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(24, 20, 24, 64);
    outer->setSpacing(0);
    // center the content column when the window is wider (web .view.max)
    auto* center = new QHBoxLayout;
    center->setContentsMargins(0, 0, 0, 0);
    center->addStretch(1);
    center->addWidget(content, 10);
    center->addStretch(1);
    auto* centerHost = new QWidget;
    centerHost->setLayout(center);
    m_scroll->setWidget(centerHost);
    rootLay->addWidget(m_scroll);

    // ── toolbar ──
    auto* toolbar = new QWidget;
    auto* tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(0, 0, 0, 12);
    tb->setSpacing(10);
    auto* h1 = new QLabel(QStringLiteral("实时监控"));
    h1->setObjectName(QStringLiteral("h1"));
    auto* sub = new QLabel(QStringLiteral("观察 agent 此刻的运行状态"));
    sub->setProperty("cls", "muted");
    sub->setStyleSheet("font-size:9pt;");
    tb->addWidget(h1);
    tb->addWidget(sub);
    tb->addStretch(1);
    m_windowCombo = new QComboBox;
    m_windowCombo->addItem(QStringLiteral("今天"), QStringLiteral("today"));
    m_windowCombo->addItem(QStringLiteral("近 24 小时"), QStringLiteral("24h"));
    m_windowCombo->addItem(QStringLiteral("近 7 天"), QStringLiteral("7d"));
    m_windowCombo->setCurrentIndex(1);
    connect(m_windowCombo, &QComboBox::currentIndexChanged, this, &OverviewPage::reload);
    tb->addWidget(m_windowCombo);
    m_refreshBtn = new QPushButton(QStringLiteral("↻ 刷新"));
    m_refreshBtn->setProperty("cls", "ghost");
    connect(m_refreshBtn, &QPushButton::clicked, this, &OverviewPage::reload);
    tb->addWidget(m_refreshBtn);
    outer->addWidget(toolbar);

    // ── KPI 区：6 列布局；今日总 token（左）与总 token 24H（右）各独占两行 ──
    auto* kpiHost = new QWidget;
    m_kpiGrid = new QGridLayout(kpiHost);
    m_kpiGrid->setContentsMargins(0, 0, 0, 0);
    m_kpiGrid->setSpacing(10);
    for (int c = 0; c < 6; ++c)
        m_kpiGrid->setColumnStretch(c, 1);
    m_kTodayTotal = new KpiCard(QStringLiteral("总 token (今日)"));
    m_kTodayTotal->setLargeValue(true);
    m_kCalls = new KpiCard(QStringLiteral("模型调用"));
    m_kAvg = new KpiCard(QStringLiteral("平均响应时延"));
    m_kIn = new KpiCard(QStringLiteral("输入 token"));
    m_kOut = new KpiCard(QStringLiteral("输出 token"));
    m_kTotal = new KpiCard(QStringLiteral("总 token"));
    m_kTotal->setLargeValue(true);
    m_kReason = new KpiCard(QStringLiteral("推理 token 占比"));
    m_kTools = new KpiCard(QStringLiteral("工具调用"));
    m_kSessions = new KpiCard(QStringLiteral("活跃会话"));
    m_kErrRate = new KpiCard(QStringLiteral("错误率"));
    m_kpiGrid->addWidget(m_kTodayTotal, 0, 0, 2, 1); // 最左，独占两行
    m_kpiGrid->addWidget(m_kCalls, 0, 1);
    m_kpiGrid->addWidget(m_kAvg, 0, 2);
    m_kpiGrid->addWidget(m_kIn, 0, 3);
    m_kpiGrid->addWidget(m_kOut, 0, 4);
    m_kpiGrid->addWidget(m_kTotal, 0, 5, 2, 1);      // 最右，独占两行
    m_kpiGrid->addWidget(m_kReason, 1, 1);
    m_kpiGrid->addWidget(m_kTools, 1, 2);
    m_kpiGrid->addWidget(m_kSessions, 1, 3);
    m_kpiGrid->addWidget(m_kErrRate, 1, 4);
    m_kpiGrid->setRowStretch(0, 1);
    m_kpiGrid->setRowStretch(1, 1);
    outer->addWidget(kpiHost);

    auto* speedGrid = new QWidget;
    auto* sg = new QVBoxLayout(speedGrid);
    sg->setContentsMargins(0, 0, 0, 0);
    m_kSpeed = new KpiCard(QStringLiteral("平均 Token 速度"));
    sg->addWidget(m_kSpeed);
    outer->addWidget(speedGrid);

    // ── 趋势 ──
    {
        auto* row = sectionLabel(QStringLiteral("趋势"));
        m_seriesRange = row->findChild<QLabel*>(QStringLiteral("sectionSub"));
        if (!m_seriesRange) {
            m_seriesRange = new QLabel;
            m_seriesRange->setObjectName(QStringLiteral("sectionSub"));
            m_seriesRange->setStyleSheet(
                QStringLiteral("color:%1;font-size:8.5pt;")
                    .arg(Theme::instance().pal().fg4.name()));
            static_cast<QBoxLayout*>(row->layout())->insertWidget(1, m_seriesRange, 1);
        }
        outer->addWidget(row);
    }
    // 四张图表各占一整行（全宽 + 加高），页面滚动浏览
    auto* card1 = new CardFrame;
    auto* c1 = new QVBoxLayout(card1);
    c1->setContentsMargins(14, 12, 14, 12);
    auto* t1 = new QLabel(QStringLiteral("模型调用 / 小时"));
    t1->setObjectName(QStringLiteral("cardTitle"));
    c1->addWidget(t1);
    m_chCalls = new MiniChart;
    m_chCalls->setMinimumHeight(300);
    c1->addWidget(m_chCalls, 1);
    outer->addWidget(card1);
    outer->addSpacing(12);

    auto* card2 = new CardFrame;
    auto* c2 = new QVBoxLayout(card2);
    c2->setContentsMargins(14, 12, 14, 12);
    auto* t2 = new QLabel(QStringLiteral("Token 构成（输入 / 输出 / 推理）"));
    t2->setObjectName(QStringLiteral("cardTitle"));
    c2->addWidget(t2);
    m_chTokens = new MiniChart;
    m_chTokens->setMinimumHeight(300);
    c2->addWidget(m_chTokens, 1);
    outer->addWidget(card2);

    // ── Token 速度 ──
    outer->addWidget(sectionLabel(QStringLiteral("Token 速度"),
                                  QStringLiteral("tokens/sec · 每次完成请求")));
    auto* cardS1 = new CardFrame;
    auto* s1 = new QVBoxLayout(cardS1);
    s1->setContentsMargins(14, 12, 14, 12);
    auto* st1 = new QLabel(QStringLiteral("速度随时间"));
    st1->setObjectName(QStringLiteral("cardTitle"));
    s1->addWidget(st1);
    m_chSpeed = new MiniChart;
    m_chSpeed->setMinimumHeight(300);
    s1->addWidget(m_chSpeed, 1);
    outer->addWidget(cardS1);
    outer->addSpacing(12);

    auto* cardS2 = new CardFrame;
    auto* s2 = new QVBoxLayout(cardS2);
    s2->setContentsMargins(0, 0, 0, 0);
    s2->setSpacing(0);
    auto* st2 = new QLabel(QStringLiteral("最近请求速度"));
    st2->setObjectName(QStringLiteral("cardTitle"));
    st2->setContentsMargins(14, 12, 14, 0);
    s2->addWidget(st2);
    m_speedWrap = new QWidget;
    auto* sw = new QVBoxLayout(m_speedWrap);
    sw->setContentsMargins(0, 0, 0, 0);
    m_speedTable = new RowHoverTable;
    m_speedTable->setColumnCount(7);
    m_speedTable->setHorizontalHeaderLabels({QStringLiteral("Time"), QStringLiteral("Model"),
                                              QStringLiteral("Output"), QStringLiteral("Reason"),
                                              QStringLiteral("Duration"), QStringLiteral("Speed"),
                                              QStringLiteral("Source")});
    m_speedTable->verticalHeader()->hide();
    m_speedTable->horizontalHeader()->setStretchLastSection(true);
    m_speedTable->setAlternatingRowColors(false);
    m_speedTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_speedTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_speedTable->setMinimumHeight(470); // 一次可见 ≥15 行（表头 + 15×~28px）
    sw->addWidget(m_speedTable);
    m_speedFoot = new QWidget;
    auto* sf = new QHBoxLayout(m_speedFoot);
    sf->setContentsMargins(12, 9, 12, 9);
    sf->setSpacing(16);
    for (int i = 0; i < 4; ++i) {
        auto* l = new QLabel;
        l->setContentsMargins(0, 0, 0, 0);
        sf->addWidget(l, 1);
    }
    s2->addWidget(m_speedWrap, 1);
    m_speedWrap->setMinimumHeight(490);
    s2->addWidget(m_speedFoot);
    outer->addWidget(cardS2);

    // ── 实时活动 ──
    {
        auto* row = sectionLabel(QStringLiteral("实时活动"),
                                 QStringLiteral("推送新发生的模型/工具调用"));
        m_liveStatus = new BadgeLabel(QStringLiteral("连接中…"), QStringLiteral("dim"));
        static_cast<QBoxLayout*>(row->layout())->addWidget(m_liveStatus);
        outer->addWidget(row);
    }
    auto* feedCard = new CardFrame;
    auto* fc = new QVBoxLayout(feedCard);
    fc->setContentsMargins(4, 4, 4, 4);
    m_feed = new LiveFeed;
    m_feed->setMinimumHeight(160);
    m_feed->setMaximumHeight(400);
    fc->addWidget(m_feed);
    outer->addWidget(feedCard);

    // ── 算力分布 ──（两表各占一整行，与图表同宽）
    outer->addWidget(sectionLabel(QStringLiteral("算力分布"), QStringLiteral("花在哪")));
    auto* bmCard = new CardFrame;
    auto* bmc = new QVBoxLayout(bmCard);
    bmc->setContentsMargins(14, 12, 14, 12);
    auto* bmt = new QLabel(QStringLiteral("按模型 / 请求来源"));
    bmt->setObjectName(QStringLiteral("cardTitle"));
    bmc->addWidget(bmt);
    m_byModelTable = new RowHoverTable;
    m_byModelTable->setColumnCount(7);
    m_byModelTable->setHorizontalHeaderLabels(
        {QStringLiteral("provider / model"), QStringLiteral("来源"), QStringLiteral("调用"),
         QStringLiteral("输入"), QStringLiteral("输出"), QStringLiteral("推理"),
         QStringLiteral("均时延")});
    m_byModelTable->verticalHeader()->hide();
    m_byModelTable->horizontalHeader()->setStretchLastSection(true);
    m_byModelTable->setFont(QFont(QStringLiteral("Segoe UI"), 10));
    m_byModelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_byModelTable->setSelectionMode(QAbstractItemView::NoSelection);
    bmc->addWidget(m_byModelTable);
    outer->addWidget(bmCard);
    outer->addSpacing(12);

    auto* btCard = new CardFrame;
    auto* btc = new QVBoxLayout(btCard);
    btc->setContentsMargins(14, 12, 14, 12);
    auto* btt = new QLabel(QStringLiteral("按工具"));
    btt->setObjectName(QStringLiteral("cardTitle"));
    btc->addWidget(btt);
    m_byToolTable = new RowHoverTable;
    m_byToolTable->setColumnCount(6);
    m_byToolTable->setHorizontalHeaderLabels(
        {QStringLiteral("工具"), QStringLiteral("调用"), QStringLiteral("错误"),
         QStringLiteral("均时延"), QStringLiteral("最大"), QStringLiteral("输出字节")});
    m_byToolTable->verticalHeader()->hide();
    m_byToolTable->horizontalHeader()->setStretchLastSection(true);
    m_byToolTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_byToolTable->setSelectionMode(QAbstractItemView::NoSelection);
    btc->addWidget(m_byToolTable);
    outer->addWidget(btCard);
    outer->addStretch(1);
}

void OverviewPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    m_poller->start();
    reload();
}

void OverviewPage::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    m_poller->stop();
}

void OverviewPage::reload()
{
    // 12 aggregate queries per refresh — run on a pool thread; a stale
    // refresh (window changed again) is dropped via the sequence number
    const QString w = m_windowCombo->currentData().toString();
    const int seq = ++m_reloadSeq;
    struct ReloadPair {
        types::OverviewData main;  // selected window
        types::OverviewData today; // fixed today (left total card)
    };
    Async::run<ReloadPair>(
        this,
        [w]() {
            ReloadPair d;
            d.main = DbService::instance().overview(w);
            d.today = DbService::instance().overview(QStringLiteral("today"));
            return d;
        },
        [this, seq](const ReloadPair& d) {
            if (seq != m_reloadSeq)
                return;
            m_data = d.main;
            m_todayData = d.today;
            m_loaded = true;
            renderData();
        });
}

void OverviewPage::renderData()
{
    if (!m_loaded)
        return;
    const types::Kpis& k = m_data.kpis;
    const Palette& pal = Theme::instance().pal();

    // ── KPI cards ──
    m_kCalls->setCaption(QStringLiteral("模型调用 (%1)").arg(windowLabel(m_data.window)));
    m_kSpeed->setCaption(QStringLiteral("平均 Token 速度 (%1)").arg(windowLabel(m_data.window)));
    m_kCalls->setValue(Format::fmtInt(double(k.calls)), pal.accent);
    m_kCalls->setDelta(QStringLiteral("完成 %1 · 失败 %2 · 取消 %3")
                           .arg(Format::fmtInt(double(k.completed)),
                                Format::fmtInt(double(k.errors)),
                                Format::fmtInt(double(k.cancelled))));
    m_kAvg->setValue(k.avgDurationOk ? Format::fmtMs(k.avgDurationMs) : QStringLiteral("—"));
    m_kAvg->setDelta(QStringLiteral("到首 token 时间另计"));

    const int cacheRate = k.inTok
        ? qMin(100, qRound(double(k.cacheRead) * 100.0 / double(k.inTok)))
        : 0;
    m_kIn->setValue(Format::fmtNum(double(k.inTok)),
                    k.inTok >= 100000000 ? pal.sevErr : pal.sevOk);
    m_kIn->setBar(cacheRate, pal.catTool2);
    m_kIn->setDelta(QStringLiteral("缓存命中 %1% · 写入 %2")
                        .arg(cacheRate)
                        .arg(Format::fmtNum(double(k.cacheWrite))));

    m_kOut->setValue(Format::fmtNum(double(k.outTok)),
                     k.outTok >= 100000000 ? pal.sevErr : QColor());
    m_kOut->setDelta(QStringLiteral("模型实际生成"));

    // 两张总 token 大卡（今日 / 所选窗口）共用同一渲染
    renderTotalCard(m_kTodayTotal, m_todayData);
    renderTotalCard(m_kTotal, m_data);

    QString reasonVal = QStringLiteral("—");
    QColor reasonColor;
    if (k.reasonRatioOk) {
        reasonVal = QString::number(k.reasonRatio * 100, 'f', 1) + '%';
        const double pct = k.reasonRatio * 100;
        reasonColor = pct > 30 ? pal.accent2 : (pct > 5 ? pal.catTool2 : pal.accent);
    }
    m_kReason->setValue(reasonVal, reasonColor);
    m_kReason->setDelta(QStringLiteral("推理 %1 / 输出 %2")
                            .arg(Format::fmtNum(double(k.reasonTok)),
                                 Format::fmtNum(double(k.outTok))));

    m_kTools->setValue(Format::fmtInt(double(k.toolCalls)), pal.catTool2);
    m_kTools->setDelta(QStringLiteral("失败 %1 · 均 %2")
                           .arg(Format::fmtInt(double(k.toolErrors)),
                                k.toolAvgOk ? Format::fmtMs(k.toolAvgMs)
                                             : QStringLiteral("—")));
    m_kSessions->setValue(Format::fmtInt(double(k.activeSessions)));
    m_kSessions->setDelta(QStringLiteral("窗口内有模型调用"));

    const double errRate = k.calls ? double(k.errors) / double(k.calls) * 100.0 : 0.0;
    m_kErrRate->setValue(QString::number(errRate, 'f', 1) + '%',
                         errRate > 5 ? pal.sevErr : QColor());
    m_kErrRate->setDelta(QStringLiteral("%1 / %2")
                             .arg(Format::fmtInt(double(k.errors)),
                                  Format::fmtInt(double(k.calls))));

    const types::SpeedStats& s = m_data.speed;
    m_kSpeed->setValue(s.weightedOk
                            ? QString::number(s.weightedTps, 'f', 1) + QStringLiteral(" t/s")
                            : QStringLiteral("—"),
                       UiUtil::speedColor(Format::speedTier(s.weightedOk ? s.weightedTps : qQNaN())));
    m_kSpeed->setDelta(QStringLiteral("加权:总 token ÷ 总秒数 · 主 %1 · 子agent %2")
                           .arg(Format::fmtInt(double(s.mainCount)),
                                Format::fmtInt(double(s.subagentCount))));
    m_kSpeed->setBar(-1, QColor());

    // ── series range label ──
    if (m_seriesRange)
        m_seriesRange->setText(m_data.window == QLatin1String("7d")
                                      ? QStringLiteral("近 7 天·按小时")
                                      : QStringLiteral("近 24 小时"));

    renderCharts();

    // ── speed table ──
    const auto& recent = m_data.recentSpeed;
    m_speedTable->setRowCount(int(recent.size()));
    for (int i = 0; i < recent.size(); ++i) {
        const types::SpeedRow& r = recent[i];
        m_speedTable->setItem(i, 0, txtItem(Format::fmtTime(r.timeMs)));
        auto* mod = txtItem(r.model.isEmpty() ? QStringLiteral("?") : r.model);
        QFont mono(QStringLiteral("Consolas"), 9);
        mod->setFont(mono);
        m_speedTable->setItem(i, 1, mod);
        m_speedTable->setItem(i, 2, numItem(Format::fmtInt(double(r.output))));
        auto* rs = numItem(Format::fmtInt(double(r.reasoning)));
        if (r.reasoning == 0)
            tint(rs, pal.fg4);
        m_speedTable->setItem(i, 3, rs);
        m_speedTable->setItem(i, 4, numItem(Format::fmtMs(double(r.durationMs))));
        auto* spd = numItem(r.tpsOk ? QString::number(r.tps, 'f', 1) + QStringLiteral(" t/s")
                                     : QStringLiteral("—"));
        if (r.tpsOk)
            tint(spd, speedTierColor(r.tps));
        else
            tint(spd, pal.fg4);
        m_speedTable->setItem(i, 5, spd);
        auto* src = txtItem(r.querySource);
        src->setForeground(pal.fg4);
        m_speedTable->setItem(i, 6, src);
    }
    m_speedTable->resizeColumnsToContents();

    // footer: recompute from the same rows (web does this client-side)
    const auto footLabels = m_speedFoot->findChildren<QLabel*>();
    if (recent.isEmpty()) {
        m_speedFoot->hide();
    } else {
        qint64 totTok = 0;
        double totSec = 0;
        qint64 subs = 0;
        for (const types::SpeedRow& r : recent) {
            totTok += r.output + r.reasoning;
            totSec += double(r.durationMs) / 1000.0;
            if (r.querySource == QLatin1String("subagent"))
                ++subs;
        }
        const QString wTps = totSec > 0 ? QString::number(totTok / totSec, 'f', 1)
                                        : QStringLiteral("—");
        const QString texts[4] = {
            QStringLiteral("均速  %1 t/s").arg(wTps),
            QStringLiteral("总 token  %1").arg(Format::fmtInt(double(totTok))),
            QStringLiteral("请求  %1").arg(Format::fmtInt(double(recent.size()))),
            QStringLiteral("subagent  %1").arg(Format::fmtInt(double(subs))),
        };
        m_speedFoot->show();
        for (int i = 0; i < qMin(4, footLabels.size()); ++i) {
            footLabels[i]->setText(texts[i]);
            if (i == 0 && totSec > 0)
                footLabels[i]->setStyleSheet(
                    QStringLiteral("color:%1;font-family:'Consolas';font-weight:600;")
                        .arg(speedTierColor(totTok / totSec).name()));
            else
                footLabels[i]->setStyleSheet(
                    QStringLiteral("color:%1;font-size:8.5pt;")
                        .arg(pal.fg3.name()));
        }
    }

    // ── by model table ──
    const auto& byModel = m_data.byModel;
    m_byModelTable->setRowCount(int(byModel.size()));
    for (int i = 0; i < byModel.size(); ++i) {
        const types::ModelBreakdown& m = byModel[i];
        auto* name = txtItem(m.modelId.isEmpty() ? QStringLiteral("?") : m.modelId);
        name->setFont(QFont(QStringLiteral("Consolas"), 10));
        QString provider = m.providerId;
        provider.remove(QStringLiteral("builtin:"));
        QString secondLine = provider;
        if (!m.variant.isEmpty())
            secondLine += QStringLiteral(" · ") + m.variant;
        if (!secondLine.isEmpty())
            name->setText(name->text() + QStringLiteral("\n") + secondLine);
        name->setToolTip(name->text());
        m_byModelTable->setItem(i, 0, name);
        auto* src = txtItem(m.querySource);
        src->setForeground(Theme::instance().badgeColor(sourceBadgeKind(m.querySource)));
        m_byModelTable->setItem(i, 1, src);
        m_byModelTable->setItem(i, 2, numItem(Format::fmtInt(double(m.calls))));
        m_byModelTable->setItem(i, 3, numItem(Format::fmtNum(double(m.inTok))));
        m_byModelTable->setItem(i, 4, numItem(Format::fmtNum(double(m.outTok))));
        m_byModelTable->setItem(i, 5, numItem(Format::fmtNum(double(m.reasonTok))));
        m_byModelTable->setItem(i, 6,
                                numItem(m.avgOk ? Format::fmtMs(m.avgMs) : QStringLiteral("—")));
    }
    if (byModel.isEmpty())
        m_byModelTable->setRowCount(0);
    m_byModelTable->resizeColumnsToContents();
    m_byModelTable->resizeRowsToContents();
    m_byModelTable->setFixedHeight(UiUtil::tableContentHeight(m_byModelTable, 900));

    // ── by tool table ──
    const auto& byTool = m_data.byTool;
    m_byToolTable->setRowCount(int(byTool.size()));
    for (int i = 0; i < byTool.size(); ++i) {
        const types::ToolBreakdown& t = byTool[i];
        auto* name = txtItem(t.toolName);
        name->setFont(QFont(QStringLiteral("Consolas"), 10));
        m_byToolTable->setItem(i, 0, name);
        m_byToolTable->setItem(i, 1, numItem(Format::fmtInt(double(t.calls))));
        auto* errs = numItem(Format::fmtInt(double(t.errors)));
        if (t.errors == 0)
            tint(errs, pal.fg4);
        else
            tint(errs, pal.sevErr);
        m_byToolTable->setItem(i, 2, errs);
        m_byToolTable->setItem(i, 3, numItem(t.avgOk ? Format::fmtMs(t.avgMs) : QStringLiteral("—")));
        m_byToolTable->setItem(i, 4, numItem(Format::fmtMs(double(t.maxMs))));
        m_byToolTable->setItem(i, 5, numItem(Format::fmtNum(double(t.outBytes))));
    }
    m_byToolTable->resizeColumnsToContents();
    m_byToolTable->setFixedHeight(UiUtil::tableContentHeight(m_byToolTable, 620));
}

// 总 token 大卡（今日 / 所选窗口共用）：中文单位总量 + 生成占比 + 每模型彩色用量
void OverviewPage::renderTotalCard(KpiCard* card, const types::OverviewData& d)
{
    const types::Kpis& k = d.kpis;
    const Palette& pal = Theme::instance().pal();
    card->setCaption(QStringLiteral("总 token (%1)").arg(windowLabel(d.window)));

    const qint64 totalTok = k.inTok + k.outTok + k.reasonTok;
    QVector<QPair<QString, qint64>> perModel;
    for (const types::ModelBreakdown& m : d.byModel)
        perModel.push_back({m.modelId.isEmpty() ? QStringLiteral("?") : m.modelId,
                            m.inTok + m.outTok + m.reasonTok});
    std::sort(perModel.begin(), perModel.end(),
              [](const QPair<QString, qint64>& a, const QPair<QString, qint64>& b) {
                  return a.second > b.second;
              });

    if (totalTok > 0) {
        const double genPct = double(k.outTok + k.reasonTok) * 100.0 / double(totalTok);
        // 上亿（≥1e8）→ 标红醒目
        const bool hot = totalTok >= 100000000;
        card->setValue(Format::fmtNumCn(double(totalTok)),
                       hot ? pal.sevErr : QColor());
        card->setBar(qMin(100, qRound(genPct)), pal.accent);
        card->setDelta(QStringLiteral("生成占 %1% · 输入 %2")
                           .arg(QString::number(genPct, 'f', 1))
                           .arg(Format::fmtNumCn(double(k.inTok))));
        const QColor dotColors[] = {pal.catConversation, pal.catUsage, pal.catLlm2,
                                    pal.catTool2, pal.catLlm};
        QStringList lines;
        for (int i = 0; i < qMin(5, perModel.size()); ++i) {
            const QColor& dc = dotColors[i % 5];
            // 单模型用量上亿同样标红
            const bool mHot = perModel[i].second >= 100000000;
            const QString vc = mHot ? pal.sevErr.name() : pal.fg1.name();
            lines << QStringLiteral(
                "<span style='color:%1'>●</span> %2 <b style='color:%3'>%4</b>")
                .arg(dc.name(), perModel[i].first.toHtmlEscaped(),
                     vc, Format::fmtNumCn(double(perModel[i].second)));
        }
        if (perModel.size() > 5)
            lines << QStringLiteral("等 %1 个模型").arg(perModel.size());
        card->setDetails(lines.join(QStringLiteral("<br/>")));
    } else {
        card->setValue(QStringLiteral("—"));
        card->setBar(-1, QColor());
        card->setDelta(QStringLiteral("输入 + 输出 + 推理"));
        card->setDetails(QString());
    }
}

void OverviewPage::renderCharts()
{
    if (!m_loaded)
        return;
    const Palette& pal = Theme::instance().pal();

    // calls / hour (bar)
    {
        QStringList labels;
        QVector<double> values;
        for (const types::SeriesPoint& p : m_data.series) {
            labels << Format::fmtTimeTick(p.bucketMs);
            values << double(p.calls);
        }
        if (labels.isEmpty())
            m_chCalls->setEmpty(QStringLiteral("无数据"));
        else
            m_chCalls->setBar(labels, values, pal.accent, QStringLiteral("调用数"),
                              MiniChart::YInt);
    }

    // token mix (multi-line with fills)
    {
        QStringList labels;
        for (const types::SeriesPoint& p : m_data.series)
            labels << Format::fmtTimeTick(p.bucketMs);
        QVector<MiniChart::Series> series;
        MiniChart::Series in, out, reason;
        in.name = QStringLiteral("输入");
        in.color = pal.catUsage;
        in.fillAlpha = 0.10;
        out.name = QStringLiteral("输出");
        out.color = pal.accent;
        out.fillAlpha = 0.08;
        reason.name = QStringLiteral("推理");
        reason.color = pal.catLlm2;
        reason.fillAlpha = 0.10;
        for (const types::SeriesPoint& p : m_data.series) {
            in.values << double(p.input);
            out.values << double(p.output);
            reason.values << double(p.reasoning);
        }
        series << in << out << reason;
        if (labels.isEmpty())
            m_chTokens->setEmpty(QStringLiteral("无数据"));
        else
            m_chTokens->setLines(labels, series, QStringLiteral("token"), MiniChart::YNum);
    }

    // speed over time (per-point colors)
    {
        QVector<types::SpeedRow> rows;
        for (int i = m_data.recentSpeed.size() - 1; i >= 0; --i) { // oldest → newest
            if (m_data.recentSpeed[i].tpsOk)
                rows.push_back(m_data.recentSpeed[i]);
        }
        if (rows.isEmpty()) {
            m_chSpeed->setEmpty(QStringLiteral("窗口内无完成请求"));
        } else {
            QStringList labels;
            MiniChart::Series s;
            s.name = QStringLiteral("tok/s");
            s.color = pal.accent;
            s.showPoints = true;
            for (const types::SpeedRow& r : rows) {
                labels << Format::fmtTimeTick(r.timeMs);
                s.values << r.tps;
                s.pointColors << UiUtil::speedColor(Format::speedTier(r.tps));
            }
            m_chSpeed->setLines(labels, {s}, QStringLiteral("tok/s"), MiniChart::YTps);
        }
    }
}

void OverviewPage::onModelRows(const QVector<types::ModelRow>& rows)
{
    m_feed->feedModel()->pushModelRows(rows);
}

void OverviewPage::onToolRows(const QVector<types::ToolRow>& rows)
{
    m_feed->feedModel()->pushToolRows(rows);
}

void OverviewPage::onLiveError(const QString&)
{
    m_liveStatus->setText(QStringLiteral("查询重试中…"));
    m_liveStatus->setKind(QStringLiteral("red"));
}

void OverviewPage::onLiveRecovered()
{
    m_liveStatus->setText(QStringLiteral("已连接"));
    m_liveStatus->setKind(QStringLiteral("green"));
}
