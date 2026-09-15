#pragma once
// ErrorsPage.h — port of public/views/errors.js: error aggregation,
// failed-call tables (click → trace), slow tools and the trace waterfall.

#include <QWidget>

#include "core/Types.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QVBoxLayout;
class QPlainTextEdit;

class WaterfallWidget : public QWidget {
    Q_OBJECT
public:
    void setEvents(const QVector<types::LogEvent>& events);
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<types::LogEvent> m_events;
    qint64 m_minTs = 0;
    qint64 m_spanMs = 1;
    static constexpr int kRowH = 22;
};

class ErrorsPage : public QWidget {
    Q_OBJECT
public:
    explicit ErrorsPage(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* e) override;

signals:
    void openSessionRequested(const QString& sessionId, const QString& tab);

private slots:
    void load();
    void runTrace();

private:
    void buildUi();
    void renderSummary(const types::ErrorSummary& s);
    void renderFailed(const types::FailedItems& items);
    void renderSlow(const QVector<types::SlowToolRow>& items);
    QTableWidget* makeTable(const QStringList& headers);
    void fillSessionLinkRow(QTableWidget* table, int row, int col,
                            const QString& sessionId);

    QComboBox* m_windowCombo = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QTableWidget* m_summaryA = nullptr;
    QTableWidget* m_summaryB = nullptr;
    QTableWidget* m_summaryC = nullptr;
    QTableWidget* m_failedModel = nullptr;
    QTableWidget* m_failedTool = nullptr;
    QTableWidget* m_slowTable = nullptr;
    QLineEdit* m_traceInput = nullptr;
    QPushButton* m_traceGo = nullptr;
    QLabel* m_traceOut = nullptr;
    QWidget* m_traceResult = nullptr;
    QVBoxLayout* m_traceLay = nullptr;
    WaterfallWidget* m_waterfall = nullptr;
    QPlainTextEdit* m_spanJson = nullptr;
    QPushButton* m_spanToggle = nullptr;
    bool m_loaded = false;
    int m_loadSeq = 0;   // async load guard: drop superseded results
    int m_traceSeq = 0;  // async trace guard: drop superseded results
};
