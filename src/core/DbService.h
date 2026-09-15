#pragma once
// DbService.h — port of server/db.js (all SQL) plus the pieces of
// routes/{overview,sessions,trace,agents}.js that enrich DB results.
// Read-only connection with the same hardening ladder as the original:
//   open retry 50/150/400/1000ms ×5 · busy_timeout=5000 ·
//   statement retry 30/60/120ms ×4 · connection-damage → reopen.

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <functional>

#include "Types.h"

class DbService : public QObject {
    Q_OBJECT
public:
    static DbService& instance();

    // ── connection management ──
    bool warm(QString* errOut = nullptr);   // open + SELECT 1 (no throw on busy)
    void invalidate();                       // drop cached connection (next use reopens)
    bool probe();                             // live health check; invalidates on failure
    qint64 startOfDayMs() const;              // local midnight
    // per-thread state: async workers run queries on pool threads
    QString lastError() const;
    bool lastQueryOk() const; // success of the most recent run() call

    // ── overview ──
    types::Kpis overviewKpis(qint64 sinceMs);
    QVector<types::SeriesPoint> timeseries(int hours);
    QVector<types::ModelBreakdown> breakdownByModel(qint64 sinceMs);
    QVector<types::ToolBreakdown> breakdownByTool(qint64 sinceMs);
    types::SpeedStats overviewSpeed(qint64 sinceMs);
    QVector<types::SpeedRow> recentSpeed(qint64 sinceMs, int limit = 50);
    types::OverviewData overview(const QString& window); // window: today|24h|7d

    // ── live feed ──
    // Tailing by rowid (insert order) — never misses late-committing rows.
    // (Tailing by started_at loses rows whose request STARTED before our
    // watermark but committed after it — the long-request case.)
    QVector<types::ModelRow> modelRowsAfterRowid(qint64 afterRowId, int limit = 100);
    QVector<types::ToolRow> toolRowsAfterRowid(qint64 afterRowId, int limit = 100);
    qint64 latestModelRowId();
    qint64 latestToolRowId();
    QVector<types::ModelRow> recentModelRows(qint64 afterStartedMs, int limit = 100);
    QVector<types::ToolRow> recentToolRows(qint64 afterStartedMs, int limit = 100);
    qint64 latestModelStartedAt();            // ORDER BY started_at DESC LIMIT 1 (watermark init)
    qint64 latestToolStartedAt();

    // ── sessions ──
    QVector<types::SessionRow> sessionList(int limit = 100, int offset = 0,
                                           const QString& q = QString(),
                                           const QString& taskType = QString());
    types::SessionDetail sessionGet(const QString& id);
    QVector<types::TurnRow> sessionTurns(const QString& id);
    QVector<types::Message> sessionConversation(const QString& id, int maxMessages = 400);
    QVector<types::ActivityRow> sessionActivity(const QString& id, int limit = 200);
    QVector<types::ReasoningItem> sessionReasoning(const QString& id, int limit = 50);
    QVector<types::ChildAgent> sessionChildrenEnriched(const QString& id);
    types::ToolOutput toolOutput(const QString& sessionId, const QString& toolCallId);
    QVector<types::TodoRow> todosForSession(const QString& sessionId);

    // ── errors & trace ──
    types::ErrorSummary errorSummary(qint64 sinceMs);       // sinceMs < 0 → no filter
    types::FailedItems errorsList(qint64 sinceMs, const QString& kind, int limit);
    QVector<types::SlowToolRow> slowTools(qint64 sinceMs, int limit);

    // ── agents forest ──
    QVector<types::AgentNode> agentsForest(const QString& projectId = QString());

    // ── how page helpers ──
    // most recent reasoning text among subagent sessions ("" if none)
    QString findReasoningSample(qint64* outTimeMs = nullptr);

    // SQL executor with the busy/broken retry ladder (used by RawService too)
    bool run(const QString& sql, const QVariantMap& binds,
             const std::function<void(QSqlQuery&)>& fn = nullptr);

private:
    explicit DbService();
    QSqlDatabase connection();
    static void sleepMs(int ms);

};
