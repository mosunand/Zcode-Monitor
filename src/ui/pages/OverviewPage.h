#pragma once
// OverviewPage.h — port of public/views/overview.js (实时监控 dashboard).

#include <QWidget>

#include "core/Types.h"

class QComboBox;
class QGridLayout;
class QTableWidget;
class KpiCard;
class MiniChart;
class LiveFeed;
class BadgeLabel;
class QLabel;
class QPushButton;
class LivePoller;
class QScrollArea;

class OverviewPage : public QWidget {
    Q_OBJECT
public:
    explicit OverviewPage(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private slots:
    void reload();
    void onModelRows(const QVector<types::ModelRow>& rows);
    void onToolRows(const QVector<types::ToolRow>& rows);
    void onLiveError(const QString& message);
    void onLiveRecovered();

private:
    void buildUi();
    void renderData();      // re-render everything from m_data
    void renderCharts();    // charts only (theme flips re-run this)

    QScrollArea* m_scroll = nullptr;
    QComboBox* m_windowCombo = nullptr;
    QPushButton* m_refreshBtn = nullptr;

    QGridLayout* m_kpiGrid = nullptr;
    KpiCard *m_kTodayTotal = nullptr, *m_kCalls = nullptr, *m_kAvg = nullptr,
            *m_kIn = nullptr, *m_kOut = nullptr, *m_kTotal = nullptr,
            *m_kReason = nullptr, *m_kTools = nullptr, *m_kSessions = nullptr,
            *m_kErrRate = nullptr, *m_kSpeed = nullptr;

    QLabel* m_seriesRange = nullptr;
    MiniChart* m_chCalls = nullptr;
    MiniChart* m_chTokens = nullptr;
    MiniChart* m_chSpeed = nullptr;

    QTableWidget* m_speedTable = nullptr;
    QWidget* m_speedFoot = nullptr;
    QWidget* m_speedWrap = nullptr;

    BadgeLabel* m_liveStatus = nullptr;
    LiveFeed* m_feed = nullptr;

    QTableWidget* m_byModelTable = nullptr;
    QTableWidget* m_byToolTable = nullptr;

    LivePoller* m_poller = nullptr;
    types::OverviewData m_data;
    types::OverviewData m_todayData; // fixed "today" window for the left total card
    bool m_loaded = false;
    int m_reloadSeq = 0; // async reload guard: drop superseded results

    void renderTotalCard(KpiCard* card, const types::OverviewData& d); // shared by both total cards
};
