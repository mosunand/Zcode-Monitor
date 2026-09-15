// DbService.cpp — port of server/db.js plus routes enrichment. See DbService.h.

#include "DbService.h"

#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSqlError>
#include <QSqlRecord>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <functional>

#include "Paths.h"

namespace {

constexpr const char* CONN = "zcmain";

bool isBusyError(const QSqlError& e)
{
    bool ok = false;
    const int code = e.nativeErrorCode().toInt(&ok);
    if (ok && (code == 5 || code == 6 || code == 8 || code == 517)) // BUSY/LOCKED/READONLY/BUSY_SNAPSHOT
        return true;
    const QString msg = e.text();
    return msg.contains(QLatin1String("database is locked"), Qt::CaseInsensitive)
        || msg.contains(QLatin1String("database table is locked"), Qt::CaseInsensitive)
        || msg.contains(QLatin1String("unable to open database"), Qt::CaseInsensitive);
}

bool isConnBroken(const QSqlError& e)
{
    bool ok = false;
    const int code = e.nativeErrorCode().toInt(&ok);
    if (ok && (code == 10 || code == 11 || code == 26)) // IOERR/CORRUPT/NOTADB
        return true;
    const QString msg = e.text();
    return msg.contains(QLatin1String("bad database"), Qt::CaseInsensitive)
        || msg.contains(QLatin1String("file is not a database"), Qt::CaseInsensitive)
        || msg.contains(QLatin1String("disk i/o"), Qt::CaseInsensitive);
}

qint64 i64v(const QSqlQuery& q, int idx)
{
    const QVariant v = q.value(idx);
    return v.isNull() ? 0 : v.toLongLong();
}

double dvl(const QSqlQuery& q, int idx, bool* ok)
{
    const QVariant v = q.value(idx);
    if (ok) *ok = !v.isNull() && v.canConvert<double>();
    return v.isNull() ? 0.0 : v.toDouble();
}

QString sv(const QSqlQuery& q, int idx)
{
    const QVariant v = q.value(idx);
    return v.isNull() ? QString() : v.toString();
}

QJsonObject jobj(const QString& s)
{
    if (s.isEmpty()) return QJsonObject();
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(s.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject()) return QJsonObject();
    return d.object();
}

// QJsonValue → QString, treating Null/Undefined as empty
QString js(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : QString();
}

qint64 jms(const QJsonObject& o, const char* key, bool* ok)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isString()) {
        const QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
        if (dt.isValid()) { if (ok) *ok = true; return dt.toMSecsSinceEpoch(); }
        const QDateTime dt2 = QDateTime::fromString(v.toString(), Qt::ISODate);
        if (dt2.isValid()) { if (ok) *ok = true; return dt2.toMSecsSinceEpoch(); }
    } else if (v.isDouble()) {
        if (ok) *ok = true;
        return qint64(v.toDouble());
    }
    if (ok) *ok = false;
    return 0;
}

} // namespace

DbService& DbService::instance()
{
    static DbService s;
    return s;
}

DbService::DbService() = default;

void DbService::sleepMs(int ms)
{
    QThread::msleep(unsigned(ms));
}

// ───────────────────────── connection management ─────────────────────────
// QSqlDatabase connections must not cross threads. Async loads run queries
// on pool threads, so every thread gets its OWN named connection. Error and
// ok flags are thread-local for the same reason.

namespace {
struct ThreadConn {
    QSqlDatabase db;
    QString name;
    ~ThreadConn() {
        // thread exit: close and unregister our connection. Skip when the
        // app is already gone (QSqlDatabase registry torn down first would
        // make removeDatabase crash at shutdown).
        if (QCoreApplication::instance() == nullptr)
            return;
        if (db.isValid() && !name.isEmpty()) {
            if (db.isOpen())
                db.close();
            const QString n = name;
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(n);
            name.clear();
        }
    }
};
thread_local ThreadConn t_conn;
thread_local QString t_lastError;
thread_local bool t_lastOk = true;
} // namespace

QSqlDatabase DbService::connection()
{
    if (t_conn.db.isOpen())
        return t_conn.db;

    // drop a stale handle from this thread, if any
    if (t_conn.db.isValid() || !t_conn.name.isEmpty()) {
        t_conn.db = QSqlDatabase();
        if (!t_conn.name.isEmpty()) {
            QSqlDatabase::removeDatabase(t_conn.name);
            t_conn.name.clear();
        }
    }

    // refuse early if the file is missing: SQLITE_OPEN_READONLY below would
    // otherwise turn a bad path into "no such table" errors on every query
    if (!QFileInfo::exists(Paths::dbPath())) {
        t_lastError = QStringLiteral("database file not found: ") + Paths::dbPath();
        return QSqlDatabase();
    }

    t_conn.name = QStringLiteral("zcmain-%1")
                      .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), t_conn.name);
    db.setDatabaseName(Paths::dbPath());
    // hard read-only open (SQLITE_OPEN_READONLY): never creates the file and
    // cannot write, even before PRAGMA query_only runs. query_only stays as a
    // second guard. busy_timeout mirrors the original better-sqlite3 setup.
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"));

    constexpr int attempts = 5;
    for (int i = 0; i < attempts; ++i) {
        if (db.open()) {
            {
                QSqlQuery q(db);
                q.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
                q.exec(QStringLiteral("PRAGMA query_only = ON"));
            }
            t_conn.db = db;
            return db;
        }
        t_lastError = db.lastError().text();
        // 50ms → 150ms → 450ms → 1350ms (exponential backoff, ×5 attempts)
        sleepMs(50 * int(std::pow(3.0, i)));
    }
    return QSqlDatabase();
}

void DbService::invalidate()
{
    // closes the CALLING thread's connection (checkpoint / probe paths run
    // on the GUI thread; pool threads simply open a fresh one next use)
    if (t_conn.db.isValid() && t_conn.db.isOpen())
        t_conn.db.close();
    t_conn.db = QSqlDatabase();
    if (!t_conn.name.isEmpty()) {
        QSqlDatabase::removeDatabase(t_conn.name);
        t_conn.name.clear();
    }
}

bool DbService::warm(QString* errOut)
{
    bool ok = false;
    run(QStringLiteral("SELECT 1"), {}, [&ok](QSqlQuery&) { ok = true; });
    if (!ok && errOut)
        *errOut = t_lastError;
    return ok;
}

bool DbService::probe()
{
    bool ok = false;
    run(QStringLiteral("SELECT 1"), {}, [&ok](QSqlQuery&) { ok = true; });
    if (!ok)
        invalidate(); // drop a damaged connection so the next call reopens
    return ok;
}

bool DbService::run(const QString& sql, const QVariantMap& binds,
                    const std::function<void(QSqlQuery&)>& fn)
{
    constexpr int maxAttempts = 4;
    for (int i = 0; i < maxAttempts; ++i) {
        QSqlDatabase db = connection();
        if (!db.isOpen()) {
            t_lastOk = false;
            return false;
        }

        bool retryBusy = false;
        bool retryBroken = false;
        {
            QSqlQuery q(db);
            q.setForwardOnly(true);
            if (q.prepare(sql)) {
                for (auto it = binds.constBegin(); it != binds.constEnd(); ++it)
                    q.bindValue(it.key(), it.value());
                if (q.exec()) {
                    if (fn)
                        fn(q);
                    t_lastOk = true;
                    return true;
                }
            }
            const QSqlError e = q.lastError();
            t_lastError = e.text();
            if (isConnBroken(e)) {
                retryBroken = true;
            } else if (isBusyError(e) && i < maxAttempts - 1) {
                retryBusy = true;
            } else {
                t_lastOk = false;
                return false; // hard error
            }
        } // q destroyed here — safe to invalidate below
        if (retryBroken) {
            invalidate();
            continue;
        }
        if (retryBusy) {
            sleepMs(30 * (1 << i)); // 30, 60, 120ms
            continue;
        }
    }
    t_lastOk = false;
    return false;
}

qint64 DbService::startOfDayMs() const
{
    return QDate::currentDate().startOfDay().toMSecsSinceEpoch();
}

// ───────────────────────── overview ─────────────────────────

types::Kpis DbService::overviewKpis(qint64 sinceMs)
{
    types::Kpis k;
    k.sinceMs = sinceMs;
    k.windowMs = QDateTime::currentMSecsSinceEpoch() - sinceMs;

    run(QStringLiteral(
            "SELECT COUNT(*) AS model_calls,"
            " SUM(CASE WHEN status='completed' THEN 1 END) AS completed,"
            " SUM(CASE WHEN status='error' THEN 1 END) AS errors,"
            " SUM(CASE WHEN status='cancelled' THEN 1 END) AS cancelled,"
            " SUM(input_tokens) AS in_tok,"
            " SUM(output_tokens) AS out_tok,"
            " SUM(reasoning_tokens) AS reason_tok,"
            " SUM(cache_read_input_tokens) AS cache_read,"
            " SUM(cache_creation_input_tokens) AS cache_write,"
            " AVG(CASE WHEN duration_ms IS NOT NULL AND status='completed' THEN duration_ms END) AS avg_ms"
            " FROM model_usage WHERE started_at >= :since"),
        {{QStringLiteral(":since"), sinceMs}},
        [&](QSqlQuery& q) {
            if (!q.next()) return;
            k.calls = i64v(q, 0);
            k.completed = i64v(q, 1);
            k.errors = i64v(q, 2);
            k.cancelled = i64v(q, 3);
            k.inTok = i64v(q, 4);
            k.outTok = i64v(q, 5);
            k.reasonTok = i64v(q, 6);
            k.cacheRead = i64v(q, 7);
            k.cacheWrite = i64v(q, 8);
            k.avgDurationMs = dvl(q, 9, &k.avgDurationOk);
        });

    run(QStringLiteral(
            "SELECT COUNT(*) AS tool_calls,"
            " SUM(CASE WHEN status='error' THEN 1 END) AS tool_errors,"
            " AVG(CASE WHEN status='completed' THEN duration_ms END) AS avg_tool_ms"
            " FROM tool_usage WHERE started_at >= :since"),
        {{QStringLiteral(":since"), sinceMs}},
        [&](QSqlQuery& q) {
            if (!q.next()) return;
            k.toolCalls = i64v(q, 0);
            k.toolErrors = i64v(q, 1);
            k.toolAvgMs = dvl(q, 2, &k.toolAvgOk);
        });

    run(QStringLiteral(
            "SELECT COUNT(DISTINCT session_id) AS active_sessions"
            " FROM model_usage WHERE started_at >= :since"),
        {{QStringLiteral(":since"), sinceMs}},
        [&](QSqlQuery& q) {
            if (q.next()) k.activeSessions = i64v(q, 0);
        });

    if (k.reasonTok && k.outTok) {
        k.reasonRatioOk = true;
        k.reasonRatio = double(k.reasonTok) / double(k.reasonTok + k.outTok);
    }
    return k;
}

QVector<types::SeriesPoint> DbService::timeseries(int hours)
{
    // query actual buckets into a map …
    QHash<qint64, types::SeriesPoint> byBucket;
    const qint64 sinceMs = QDateTime::currentMSecsSinceEpoch() - qint64(hours) * 3600000;
    run(QStringLiteral(
            "SELECT (started_at / 3600000) * 3600000 AS bucket,"
            " COUNT(*) AS calls,"
            " SUM(input_tokens) AS in_tok,"
            " SUM(output_tokens) AS out_tok,"
            " SUM(reasoning_tokens) AS reason_tok,"
            " SUM(CASE WHEN status='error' THEN 1 END) AS errors"
            " FROM model_usage WHERE started_at >= :since"
            " GROUP BY bucket ORDER BY bucket ASC"),
        {{QStringLiteral(":since"), sinceMs}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::SeriesPoint p;
                p.bucketMs = i64v(q, 0);
                p.calls = i64v(q, 1);
                p.input = i64v(q, 2);
                p.output = i64v(q, 3);
                p.reasoning = i64v(q, 4);
                p.errors = i64v(q, 5);
                byBucket.insert(p.bucketMs, p);
            }
        });

    // … then zero-fill every hour of the window, so hours with no activity
    // still occupy their slot on the x axis (no gaps in the chart)
    QVector<types::SeriesPoint> out;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastBucket = (nowMs / 3600000) * 3600000;
    const qint64 firstBucket = lastBucket - qint64(hours - 1) * 3600000;
    for (qint64 b = firstBucket; b <= lastBucket; b += 3600000) {
        if (byBucket.contains(b))
            out.push_back(byBucket.value(b));
        else
            out.push_back(types::SeriesPoint{b, 0, 0, 0, 0, 0});
    }
    return out;
}

QVector<types::ModelBreakdown> DbService::breakdownByModel(qint64 sinceMs)
{
    QVector<types::ModelBreakdown> out;
    run(QStringLiteral(
            "SELECT provider_id, model_id, variant, query_source,"
            " COUNT(*) AS calls,"
            " SUM(input_tokens) AS in_tok,"
            " SUM(output_tokens) AS out_tok,"
            " SUM(reasoning_tokens) AS reason_tok,"
            " AVG(CASE WHEN status='completed' THEN duration_ms END) AS avg_ms"
            " FROM model_usage WHERE started_at >= :since"
            " GROUP BY provider_id, model_id, variant, query_source"
            " ORDER BY calls DESC"),
        {{QStringLiteral(":since"), sinceMs}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::ModelBreakdown m;
                m.providerId = sv(q, 0);
                m.modelId = sv(q, 1);
                m.variant = sv(q, 2);
                m.querySource = sv(q, 3);
                m.calls = i64v(q, 4);
                m.inTok = i64v(q, 5);
                m.outTok = i64v(q, 6);
                m.reasonTok = i64v(q, 7);
                m.avgMs = dvl(q, 8, &m.avgOk);
                out.push_back(m);
            }
        });
    return out;
}

QVector<types::ToolBreakdown> DbService::breakdownByTool(qint64 sinceMs)
{
    QVector<types::ToolBreakdown> out;
    run(QStringLiteral(
            "SELECT tool_name,"
            " COUNT(*) AS calls,"
            " SUM(CASE WHEN status='error' THEN 1 END) AS errors,"
            " AVG(CASE WHEN status='completed' THEN duration_ms END) AS avg_ms,"
            " MAX(duration_ms) AS max_ms,"
            " SUM(output_bytes) AS out_bytes"
            " FROM tool_usage WHERE started_at >= :since"
            " GROUP BY tool_name ORDER BY calls DESC"),
        {{QStringLiteral(":since"), sinceMs}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::ToolBreakdown t;
                t.toolName = sv(q, 0);
                t.calls = i64v(q, 1);
                t.errors = i64v(q, 2);
                t.avgMs = dvl(q, 3, &t.avgOk);
                t.maxMs = i64v(q, 4);
                t.outBytes = i64v(q, 5);
                out.push_back(t);
            }
        });
    return out;
}

types::SpeedStats DbService::overviewSpeed(qint64 sinceMs)
{
    types::SpeedStats s;
    run(QStringLiteral(
            "SELECT SUM(output_tokens + COALESCE(reasoning_tokens, 0)) AS total_tokens,"
            " SUM(duration_ms) AS total_ms,"
            " COUNT(*) AS request_count,"
            " SUM(CASE WHEN query_source='main_turn' THEN 1 END) AS main_count,"
            " SUM(CASE WHEN query_source='subagent'  THEN 1 END) AS subagent_count"
            " FROM model_usage"
            " WHERE status = 'completed' AND duration_ms > 0 AND started_at >= :since"),
        {{QStringLiteral(":since"), sinceMs}},
        [&](QSqlQuery& q) {
            if (!q.next()) return;
            s.totalTokens = i64v(q, 0);
            const qint64 totalMs = i64v(q, 1);
            s.totalSeconds = double(totalMs) / 1000.0;
            s.requestCount = i64v(q, 2);
            s.mainCount = i64v(q, 3);
            s.subagentCount = i64v(q, 4);
            if (s.totalSeconds > 0) {
                s.weightedOk = true;
                s.weightedTps = s.totalTokens / s.totalSeconds;
            }
        });
    return s;
}

QVector<types::SpeedRow> DbService::recentSpeed(qint64 sinceMs, int limit)
{
    QVector<types::SpeedRow> out;
    run(QStringLiteral(
            "SELECT started_at, model_id, output_tokens,"
            " COALESCE(reasoning_tokens, 0) AS reasoning_tokens,"
            " duration_ms, query_source"
            " FROM model_usage"
            " WHERE status = 'completed' AND duration_ms > 0 AND started_at >= :since"
            " ORDER BY started_at DESC LIMIT :limit"),
        {{QStringLiteral(":since"), sinceMs}, {QStringLiteral(":limit"), limit}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::SpeedRow r;
                r.timeMs = i64v(q, 0);
                r.model = sv(q, 1);
                r.output = i64v(q, 2);
                r.reasoning = i64v(q, 3);
                r.durationMs = i64v(q, 4);
                r.querySource = sv(q, 5);
                if (r.durationMs > 0) {
                    r.tpsOk = true;
                    r.tps = double(r.output + r.reasoning) / (double(r.durationMs) / 1000.0);
                }
                out.push_back(r);
            }
        });
    return out;
}

types::OverviewData DbService::overview(const QString& window)
{
    types::OverviewData d;
    d.window = window;
    qint64 sinceMs;
    if (window == QLatin1String("today"))
        sinceMs = startOfDayMs();
    else if (window == QLatin1String("7d"))
        sinceMs = QDateTime::currentMSecsSinceEpoch() - 7LL * 86400000;
    else
        sinceMs = QDateTime::currentMSecsSinceEpoch() - 24LL * 3600000;

    d.kpis = overviewKpis(sinceMs);
    d.series = timeseries(window == QLatin1String("7d") ? 24 * 7 : 24);
    d.byModel = breakdownByModel(sinceMs);
    d.byTool = breakdownByTool(sinceMs);
    d.speed = overviewSpeed(sinceMs);
    d.recentSpeed = recentSpeed(sinceMs, 50);
    d.sinceMs = sinceMs;
    return d;
}

// ───────────────────────── live feed ─────────────────────────

QVector<types::ModelRow> DbService::recentModelRows(qint64 afterStartedMs, int limit)
{
    QVector<types::ModelRow> out;
    run(QStringLiteral(
            "SELECT id, session_id, turn_id, trace_id, status, started_at, duration_ms,"
            " query_source, model_id, variant, mode, agent,"
            " input_tokens, output_tokens, reasoning_tokens, tool_call_count, error_type"
            " FROM model_usage WHERE started_at > :after"
            " ORDER BY started_at ASC LIMIT :limit"),
        {{QStringLiteral(":after"), afterStartedMs}, {QStringLiteral(":limit"), limit}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::ModelRow m;
                m.id = i64v(q, 0);
                m.sessionId = sv(q, 1);
                m.turnId = sv(q, 2);
                m.traceId = sv(q, 3);
                m.status = sv(q, 4);
                m.startedMs = i64v(q, 5);
                m.durationMs = i64v(q, 6);
                m.querySource = sv(q, 7);
                m.modelId = sv(q, 8);
                m.variant = sv(q, 9);
                m.mode = sv(q, 10);
                m.agent = sv(q, 11);
                m.inputTokens = i64v(q, 12);
                m.outputTokens = i64v(q, 13);
                m.reasoningTokens = i64v(q, 14);
                m.toolCallCount = i64v(q, 15);
                m.errorType = sv(q, 16);
                out.push_back(m);
            }
        });
    return out;
}

QVector<types::ToolRow> DbService::recentToolRows(qint64 afterStartedMs, int limit)
{
    QVector<types::ToolRow> out;
    run(QStringLiteral(
            "SELECT id, session_id, turn_id, trace_id, tool_call_id, tool_name, status,"
            " started_at, duration_ms, exit_code, error_type"
            " FROM tool_usage WHERE started_at > :after"
            " ORDER BY started_at ASC LIMIT :limit"),
        {{QStringLiteral(":after"), afterStartedMs}, {QStringLiteral(":limit"), limit}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::ToolRow t;
                t.id = i64v(q, 0);
                t.sessionId = sv(q, 1);
                t.turnId = sv(q, 2);
                t.traceId = sv(q, 3);
                t.toolCallId = sv(q, 4);
                t.toolName = sv(q, 5);
                t.status = sv(q, 6);
                t.startedMs = i64v(q, 7);
                t.durationMs = i64v(q, 8);
                const QVariant exitVar = q.value(9);
                if (!exitVar.isNull()) { t.hasExit = true; t.exitCode = exitVar.toLongLong(); }
                t.errorType = sv(q, 10);
                out.push_back(t);
            }
        });
    return out;
}

qint64 DbService::latestModelStartedAt()
{
    qint64 v = 0;
    run(QStringLiteral("SELECT started_at FROM model_usage ORDER BY started_at DESC LIMIT 1"),
        {}, [&](QSqlQuery& q) { if (q.next()) v = i64v(q, 0); });
    return v;
}

qint64 DbService::latestToolStartedAt()
{
    qint64 v = 0;
    run(QStringLiteral("SELECT started_at FROM tool_usage ORDER BY started_at DESC LIMIT 1"),
        {}, [&](QSqlQuery& q) { if (q.next()) v = i64v(q, 0); });
    return v;
}

qint64 DbService::latestModelRowId()
{
    qint64 v = 0;
    run(QStringLiteral("SELECT MAX(rowid) FROM model_usage"),
        {}, [&](QSqlQuery& q) { if (q.next()) v = i64v(q, 0); });
    return v;
}

qint64 DbService::latestToolRowId()
{
    qint64 v = 0;
    run(QStringLiteral("SELECT MAX(rowid) FROM tool_usage"),
        {}, [&](QSqlQuery& q) { if (q.next()) v = i64v(q, 0); });
    return v;
}

// tail by insert order (rowid): committed rows arrive in rid order, so a
// rid watermark guarantees every row is delivered exactly once — even rows
// whose started_at predates our watermark (long in-flight requests commit
// late with an early started_at)
QVector<types::ModelRow> DbService::modelRowsAfterRowid(qint64 afterRowId, int limit)
{
    QVector<types::ModelRow> out;
    run(QStringLiteral(
            "SELECT rowid, id, session_id, turn_id, trace_id, status, started_at, duration_ms,"
            " query_source, model_id, variant, mode, agent,"
            " input_tokens, output_tokens, reasoning_tokens, tool_call_count, error_type"
            " FROM model_usage WHERE rowid > :after"
            " ORDER BY rowid ASC LIMIT :limit"),
        {{QStringLiteral(":after"), afterRowId}, {QStringLiteral(":limit"), limit}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::ModelRow m;
                m.tailRid = i64v(q, 0);
                m.id = i64v(q, 1);
                m.sessionId = sv(q, 2);
                m.turnId = sv(q, 3);
                m.traceId = sv(q, 4);
                m.status = sv(q, 5);
                m.startedMs = i64v(q, 6);
                m.durationMs = i64v(q, 7);
                m.querySource = sv(q, 8);
                m.modelId = sv(q, 9);
                m.variant = sv(q, 10);
                m.mode = sv(q, 11);
                m.agent = sv(q, 12);
                m.inputTokens = i64v(q, 13);
                m.outputTokens = i64v(q, 14);
                m.reasoningTokens = i64v(q, 15);
                m.toolCallCount = i64v(q, 16);
                m.errorType = sv(q, 17);
                out.push_back(m);
            }
        });
    return out;
}

QVector<types::ToolRow> DbService::toolRowsAfterRowid(qint64 afterRowId, int limit)
{
    QVector<types::ToolRow> out;
    run(QStringLiteral(
            "SELECT rowid, id, session_id, turn_id, trace_id, tool_call_id, tool_name, status,"
            " started_at, duration_ms, exit_code, error_type"
            " FROM tool_usage WHERE rowid > :after"
            " ORDER BY rowid ASC LIMIT :limit"),
        {{QStringLiteral(":after"), afterRowId}, {QStringLiteral(":limit"), limit}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::ToolRow t;
                t.tailRid = i64v(q, 0);
                t.id = i64v(q, 1);
                t.sessionId = sv(q, 2);
                t.turnId = sv(q, 3);
                t.traceId = sv(q, 4);
                t.toolCallId = sv(q, 5);
                t.toolName = sv(q, 6);
                t.status = sv(q, 7);
                t.startedMs = i64v(q, 8);
                t.durationMs = i64v(q, 9);
                const QVariant exitVar = q.value(10);
                if (!exitVar.isNull()) { t.hasExit = true; t.exitCode = exitVar.toLongLong(); }
                t.errorType = sv(q, 11);
                out.push_back(t);
            }
        });
    return out;
}

// ───────────────────────── sessions ─────────────────────────

QVector<types::SessionRow> DbService::sessionList(int limit, int offset, const QString& q, const QString& taskType)
{
    QVector<types::SessionRow> out;
    QStringList where;
    QVariantMap binds;
    binds.insert(QStringLiteral(":limit"), limit);
    binds.insert(QStringLiteral(":offset"), offset);
    if (!q.isEmpty()) {
        where << QStringLiteral("(title LIKE :q OR id LIKE :q)");
        binds.insert(QStringLiteral(":q"), QStringLiteral("%") + q + QStringLiteral("%"));
    }
    if (!taskType.isEmpty()) {
        where << QStringLiteral("task_type = :taskType");
        binds.insert(QStringLiteral(":taskType"), taskType);
    }
    const QString whereSql = where.isEmpty() ? QString() : QStringLiteral("WHERE ") + where.join(QStringLiteral(" AND "));
    run(QStringLiteral(
            "SELECT s.id, s.title, s.task_type, s.directory, s.parent_id,"
            " s.time_created, s.time_updated,"
            " (SELECT COUNT(*) FROM model_usage m WHERE m.session_id = s.id) AS model_calls,"
            " (SELECT COUNT(*) FROM tool_usage  t WHERE t.session_id = s.id) AS tool_calls,"
            " (SELECT SUM(m.computed_total_tokens) FROM model_usage m WHERE m.session_id = s.id) AS total_tokens"
            " FROM session s ") + whereSql +
            QStringLiteral(" ORDER BY s.time_updated DESC LIMIT :limit OFFSET :offset"),
        binds,
        [&](QSqlQuery& qs) {
            while (qs.next()) {
                types::SessionRow s;
                s.id = sv(qs, 0);
                s.title = sv(qs, 1);
                s.taskType = sv(qs, 2);
                s.directory = sv(qs, 3);
                s.parentId = sv(qs, 4);
                s.timeCreated = i64v(qs, 5);
                s.timeUpdated = i64v(qs, 6);
                s.modelCalls = i64v(qs, 7);
                s.toolCalls = i64v(qs, 8);
                const QVariant tv = qs.value(9);
                if (!tv.isNull()) { s.totalTokensOk = true; s.totalTokens = tv.toLongLong(); }
                out.push_back(s);
            }
        });
    return out;
}

types::SessionDetail DbService::sessionGet(const QString& id)
{
    types::SessionDetail d;
    run(QStringLiteral("SELECT * FROM session WHERE id = :id"),
        {{QStringLiteral(":id"), id}},
        [&](QSqlQuery& q) {
            if (!q.next()) return;
            d.found = true;
            const QSqlRecord rec = q.record();
            for (int i = 0; i < rec.count(); ++i)
                d.allColumns.insert(rec.fieldName(i), q.value(i));
            auto colStr = [&rec, &q](const char* name) {
                const int i = rec.indexOf(QLatin1String(name));
                return i < 0 ? QString() : (q.value(i).isNull() ? QString() : q.value(i).toString());
            };
            auto colMs = [&rec, &q](const char* name) {
                const int i = rec.indexOf(QLatin1String(name));
                return i < 0 ? qint64(0) : q.value(i).toLongLong();
            };
            d.id = colStr("id");
            d.title = colStr("title");
            d.taskType = colStr("task_type");
            d.parentId = colStr("parent_id");
            d.workspaceId = colStr("workspace_id");
            d.projectId = colStr("project_id");
            d.directory = colStr("directory");
            d.permission = colStr("permission");
            d.traceId = colStr("trace_id");
            d.timeCreated = colMs("time_created");
            d.timeUpdated = colMs("time_updated");
        });
    return d;
}

QVector<types::TurnRow> DbService::sessionTurns(const QString& id)
{
    QVector<types::TurnRow> out;
    run(QStringLiteral(
            "SELECT turn_id, status, trace_id, user_message_id,"
            " started_at, first_token_at, completed_at,"
            " duration_ms, time_to_first_token_ms,"
            " model_request_count, model_retry_count,"
            " tool_call_count, tool_error_count,"
            " input_tokens, output_tokens, reasoning_tokens,"
            " cache_read_input_tokens, cache_creation_input_tokens, computed_total_tokens,"
            " context_exceeded, error_type, error_code"
            " FROM turn_usage WHERE session_id = :id ORDER BY started_at ASC"),
        {{QStringLiteral(":id"), id}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::TurnRow t;
                t.turnId = sv(q, 0);
                t.status = sv(q, 1);
                t.traceId = sv(q, 2);
                t.userMessageId = sv(q, 3);
                t.startedMs = i64v(q, 4);
                t.firstTokenMs = i64v(q, 5);
                t.completedMs = i64v(q, 6);
                const QVariant dv = q.value(7);
                if (!dv.isNull()) { t.durationOk = true; t.durationMs = dv.toLongLong(); }
                t.ttftMs = i64v(q, 8);
                t.modelRequestCount = i64v(q, 9);
                t.modelRetryCount = i64v(q, 10);
                t.toolCallCount = i64v(q, 11);
                t.toolErrorCount = i64v(q, 12);
                t.inputTokens = i64v(q, 13);
                t.outputTokens = i64v(q, 14);
                t.reasoningTokens = i64v(q, 15);
                t.cacheReadTokens = i64v(q, 16);
                t.cacheWriteTokens = i64v(q, 17);
                t.computedTotalTokens = i64v(q, 18);
                t.contextExceeded = i64v(q, 19) != 0;
                t.errorType = sv(q, 20);
                t.errorCode = sv(q, 21);
                out.push_back(t);
            }
        });
    return out;
}

QVector<types::Message> DbService::sessionConversation(const QString& id, int maxMessages)
{
    struct RawMsg {
        QString id;
        qint64 sequence = 0;
        qint64 timeMs = 0;
        QJsonObject data;
    };
    QVector<RawMsg> messages;

    run(QStringLiteral(
            "SELECT id, session_id, time_created, sequence, data"
            " FROM message WHERE session_id = :id"
            " ORDER BY COALESCE(sequence, 0) ASC, time_created ASC LIMIT :max"),
        {{QStringLiteral(":id"), id}, {QStringLiteral(":max"), maxMessages}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                RawMsg m;
                m.id = sv(q, 0);
                m.timeMs = i64v(q, 2);
                m.sequence = q.value(3).isNull() ? 0 : q.value(3).toLongLong();
                m.data = jobj(sv(q, 4));
                messages.push_back(m);
            }
        });
    if (messages.isEmpty())
        return {};

    // batch-load parts for all fetched messages, grouped by message index
    QHash<QString, int> indexByMsgId;
    for (int i = 0; i < messages.size(); ++i)
        indexByMsgId.insert(messages[i].id, i);
    QVector<QVector<types::Part>> partsByMsg(messages.size());

    QStringList placeholders;
    QVariantMap binds;
    for (int i = 0; i < messages.size(); ++i) {
        placeholders << QStringLiteral(":m%1").arg(i);
        binds.insert(placeholders.last(), messages[i].id);
    }
    run(QStringLiteral(
            "SELECT id, message_id, sequence, time_created, data FROM part"
            " WHERE message_id IN (") + placeholders.join(QStringLiteral(",")) +
            QStringLiteral(") ORDER BY message_id ASC, COALESCE(sequence, 0) ASC, time_created ASC"),
        binds,
        [&](QSqlQuery& q) {
            while (q.next()) {
                const QString msgId = sv(q, 1);
                const auto it = indexByMsgId.constFind(msgId);
                if (it == indexByMsgId.constEnd())
                    continue;
                types::Part p;
                p.id = sv(q, 0);
                p.sequence = q.value(2).isNull() ? 0 : q.value(2).toLongLong();
                p.timeCreatedMs = i64v(q, 3);
                p.data = jobj(sv(q, 4));
                partsByMsg[it.value()].push_back(p);
            }
        });

    // assemble Message structs
    QVector<types::Message> out;
    out.reserve(messages.size());
    for (int i = 0; i < messages.size(); ++i) {
        const RawMsg& m = messages[i];
        types::Message msg;
        msg.id = m.id;
        msg.sequence = m.sequence;
        msg.timeCreatedMs = m.timeMs;
        msg.role = js(m.data, "role");
        msg.model = js(m.data, "modelID");
        if (msg.model.isEmpty())
            msg.model = js(m.data.value(QLatin1String("model")).toObject(), "modelID");
        msg.provider = js(m.data, "providerID");
        if (msg.provider.isEmpty())
            msg.provider = js(m.data.value(QLatin1String("model")).toObject(), "providerID");
        msg.variant = js(m.data, "variant");
        msg.mode = js(m.data, "mode");
        msg.agent = js(m.data, "agent");
        if (m.data.contains(QLatin1String("tokens")))
            msg.tokens = m.data.value(QLatin1String("tokens")).toObject();
        msg.turnId = js(m.data.value(QLatin1String("anchor")).toObject(), "turnId");
        msg.env = m.data.value(QLatin1String("contextSnapshot")).toObject()
                      .value(QLatin1String("envInfo")).toObject();
        msg.parts = partsByMsg.value(i);
        out.push_back(msg);
    }
    return out;
}

QVector<types::ActivityRow> DbService::sessionActivity(const QString& id, int limit)
{
    QVector<types::ActivityRow> rows;
    rows.reserve(2 * size_t(limit));

    run(QStringLiteral(
            "SELECT 'model' AS kind, id, turn_id, status, started_at, completed_at, duration_ms,"
            " query_source, model_id, provider_id, variant, mode, agent,"
            " input_tokens, output_tokens, reasoning_tokens,"
            " tool_call_count, error_type, error_message"
            " FROM model_usage WHERE session_id = :id"
            " ORDER BY started_at DESC LIMIT :limit"),
        {{QStringLiteral(":id"), id}, {QStringLiteral(":limit"), limit}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::ActivityRow r;
                r.kind = QStringLiteral("model");
                r.id = i64v(q, 1);
                r.turnId = sv(q, 2);
                r.status = sv(q, 3);
                r.startedMs = i64v(q, 4);
                r.completedMs = i64v(q, 5);
                r.durationMs = i64v(q, 6);
                r.querySource = sv(q, 7);
                r.modelId = sv(q, 8);
                r.providerId = sv(q, 9);
                r.variant = sv(q, 10);
                r.mode = sv(q, 11);
                r.agent = sv(q, 12);
                r.inputTokens = i64v(q, 13);
                r.outputTokens = i64v(q, 14);
                r.reasoningTokens = i64v(q, 15);
                r.toolCallCount = i64v(q, 16);
                r.errorType = sv(q, 17);
                r.errorMessage = sv(q, 18);
                r.sessionId = id;
                rows.push_back(r);
            }
        });

    run(QStringLiteral(
            "SELECT 'tool' AS kind, id, turn_id, tool_call_id, tool_name, status,"
            " started_at, completed_at, duration_ms,"
            " exit_code, error_type, error_message"
            " FROM tool_usage WHERE session_id = :id"
            " ORDER BY started_at DESC LIMIT :limit"),
        {{QStringLiteral(":id"), id}, {QStringLiteral(":limit"), limit}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::ActivityRow r;
                r.kind = QStringLiteral("tool");
                r.id = i64v(q, 1);
                r.turnId = sv(q, 2);
                r.toolCallId = sv(q, 3);
                r.toolName = sv(q, 4);
                r.status = sv(q, 5);
                r.startedMs = i64v(q, 6);
                r.completedMs = i64v(q, 7);
                r.durationMs = i64v(q, 8);
                const QVariant ev = q.value(9);
                if (!ev.isNull()) { r.hasExit = true; r.exitCode = ev.toLongLong(); }
                r.errorType = sv(q, 10);
                r.errorMessage = sv(q, 11);
                r.sessionId = id;
                rows.push_back(r);
            }
        });

    std::sort(rows.begin(), rows.end(),
              [](const types::ActivityRow& a, const types::ActivityRow& b) {
                  return a.startedMs > b.startedMs;
              });
    if (rows.size() > limit)
        rows.resize(limit);
    return rows;
}

QVector<types::ReasoningItem> DbService::sessionReasoning(const QString& id, int limit)
{
    QVector<types::ReasoningItem> out;
    run(QStringLiteral(
            "SELECT p.id, p.message_id, p.time_created, p.data"
            " FROM part p JOIN message m ON m.id = p.message_id"
            " WHERE m.session_id = :id AND json_extract(p.data, '$.type') = 'reasoning'"
            " ORDER BY p.time_created ASC LIMIT :limit"),
        {{QStringLiteral(":id"), id}, {QStringLiteral(":limit"), limit}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::ReasoningItem r;
                r.id = sv(q, 0);
                r.messageId = sv(q, 1);
                r.timeMs = i64v(q, 2);
                const QJsonObject d = jobj(sv(q, 3));
                r.text = js(d, "text");
                const QJsonObject time = d.value(QLatin1String("time")).toObject();
                if (time.contains(QLatin1String("start"))) {
                    r.timeStartMs = jms(time, "start", &r.timeStartOk);
                }
                if (time.contains(QLatin1String("end"))) {
                    r.timeEndMs = jms(time, "end", &r.timeEndOk);
                }
                out.push_back(r);
            }
        });
    return out;
}

QVector<types::ChildAgent> DbService::sessionChildrenEnriched(const QString& id)
{
    QVector<types::ChildAgent> out;

    // SQL part (sessionChildren in db.js)
    run(QStringLiteral(
            "SELECT id, title, task_type, time_created, time_updated,"
            " (SELECT SUM(computed_total_tokens) FROM model_usage m WHERE m.session_id = c.id) AS total_tokens"
            " FROM session c WHERE c.parent_id = :id ORDER BY time_created ASC"),
        {{QStringLiteral(":id"), id}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::ChildAgent c;
                c.id = sv(q, 0);
                c.title = sv(q, 1);
                c.taskType = sv(q, 2);
                c.timeCreatedMs = i64v(q, 3);
                c.timeUpdatedMs = i64v(q, 4);
                const QVariant tv = q.value(5);
                if (!tv.isNull()) { c.totalTokensOk = true; c.totalTokens = tv.toLongLong(); }
                out.push_back(c);
            }
        });

    // metadata.json enrichment (routes/sessions.js): scan agents/<id>/agent_*/
    const QDir parentDir(Paths::agentsDir() + QLatin1Char('/') + id);
    if (parentDir.exists()) {
        const QStringList subs = parentDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& sub : subs) {
            const QString metaPath = parentDir.filePath(sub + QStringLiteral("/metadata.json"));
            if (!QFile::exists(metaPath))
                continue;
            QFile f(metaPath);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QJsonObject m = QJsonDocument::fromJson(f.readAll()).object();
            const QString childSessionId = js(m, "childSessionId");
            for (types::ChildAgent& c : out) {
                if (c.id == childSessionId) {
                    c.profile = js(m, "profileId");
                    if (m.contains(QLatin1String("profileSnapshot")))
                        c.profileSnapshot = m.value(QLatin1String("profileSnapshot")).toObject();
                    c.prompt = js(m, "prompt");
                    c.parentToolUseId = js(m, "parentToolUseId");
                    break;
                }
            }
        }
    }
    return out;
}

types::ToolOutput DbService::toolOutput(const QString& sessionId, const QString& toolCallId)
{
    types::ToolOutput out;
    const QString sessDir = Paths::execDir() + QLatin1Char('/') + sessionId;
    if (!QFileInfo::exists(sessDir))
        return out;
    out.found = true;

    auto readCapped = [](const QString& path, int maxChars) {
        QString s;
        bool truncated = false;
        QFile f(path);
        if (f.exists() && f.open(QIODevice::ReadOnly)) {
            s = QString::fromUtf8(f.readAll());
            if (s.size() > maxChars) {
                truncated = true;
                s = s.left(maxChars)
                  + QStringLiteral("\n…[truncated %1 bytes]").arg(s.size() - maxChars);
            }
        }
        return QPair<QString, bool>(s, truncated);
    };

    const auto so = readCapped(sessDir + QLatin1Char('/') + toolCallId + QStringLiteral("-stdout.log"),
                               200 * 1024);
    const auto se = readCapped(sessDir + QLatin1Char('/') + toolCallId + QStringLiteral("-stderr.log"),
                               64 * 1024);
    out.stdout_ = so.first;
    out.truncated = so.second;
    out.stderr_ = se.first;
    return out;
}

QVector<types::TodoRow> DbService::todosForSession(const QString& sessionId)
{
    QVector<types::TodoRow> out;
    run(QStringLiteral(
            "SELECT status, priority, content FROM todo"
            " WHERE session_id = :id ORDER BY position ASC LIMIT 200"),
        {{QStringLiteral(":id"), sessionId}},
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::TodoRow t;
                t.status = sv(q, 0);
                t.priority = sv(q, 1);
                t.content = sv(q, 2);
                out.push_back(t);
            }
        });
    return out;
}

// ───────────────────────── errors & trace ─────────────────────────

types::ErrorSummary DbService::errorSummary(qint64 sinceMs)
{
    types::ErrorSummary out;
    const QString failed = QStringLiteral("('error','cancelled')");
    const QString since = sinceMs >= 0
        ? QStringLiteral("started_at >= ") + QString::number(sinceMs) + QStringLiteral(" AND ")
        : QString();

    run(QStringLiteral(
            "SELECT COALESCE(error_type,'(none)') AS k, COUNT(*) AS n FROM model_usage"
            " WHERE ") + since + QStringLiteral("status IN ") + failed +
            QStringLiteral(" GROUP BY k ORDER BY n DESC"),
        {}, [&](QSqlQuery& q) {
            while (q.next()) out.byModelErrorType.push_back({sv(q, 0), i64v(q, 1)});
        });
    run(QStringLiteral(
            "SELECT tool_name AS k, COUNT(*) AS n FROM tool_usage"
            " WHERE ") + since + QStringLiteral("status IN ") + failed +
            QStringLiteral(" GROUP BY k ORDER BY n DESC"),
        {}, [&](QSqlQuery& q) {
            while (q.next()) out.byToolName.push_back({sv(q, 0), i64v(q, 1)});
        });
    run(QStringLiteral(
            "SELECT COALESCE(error_type,'(none)') AS k, COUNT(*) AS n FROM tool_usage"
            " WHERE ") + since + QStringLiteral("status IN ") + failed +
            QStringLiteral(" GROUP BY k ORDER BY n DESC"),
        {}, [&](QSqlQuery& q) {
            while (q.next()) out.byToolErrorType.push_back({sv(q, 0), i64v(q, 1)});
        });
    return out;
}

types::FailedItems DbService::errorsList(qint64 sinceMs, const QString& kind, int limit)
{
    types::FailedItems out;
    const QString sinceClause = sinceMs >= 0
        ? QStringLiteral("AND started_at >= :since") : QString();
    const QVariantMap binds = sinceMs >= 0
        ? QVariantMap{{QStringLiteral(":since"), sinceMs},
                       {QStringLiteral(":limit"), limit}}
        : QVariantMap{{QStringLiteral(":limit"), limit}};

    if (kind == QLatin1String("both") || kind == QLatin1String("model")) {
        run(QStringLiteral(
                "SELECT id, session_id, turn_id, trace_id, status, started_at,"
                " model_id, provider_id, query_source,"
                " input_tokens, output_tokens, duration_ms,"
                " error_type, error_code, error_message"
                " FROM model_usage WHERE status IN ('error','cancelled') ") + sinceClause +
                QStringLiteral(" ORDER BY started_at DESC LIMIT :limit"),
            binds,
            [&](QSqlQuery& q) {
                while (q.next()) {
                    types::FailedModelRow m;
                    m.id = i64v(q, 0);
                    m.sessionId = sv(q, 1);
                    m.turnId = sv(q, 2);
                    m.traceId = sv(q, 3);
                    m.status = sv(q, 4);
                    m.startedMs = i64v(q, 5);
                    m.modelId = sv(q, 6);
                    m.providerId = sv(q, 7);
                    m.querySource = sv(q, 8);
                    m.inputTokens = i64v(q, 9);
                    m.outputTokens = i64v(q, 10);
                    const QVariant dv = q.value(11);
                    if (!dv.isNull()) { m.durationOk = true; m.durationMs = dv.toLongLong(); }
                    m.errorType = sv(q, 12);
                    m.errorCode = sv(q, 13);
                    m.errorMessage = sv(q, 14);
                    out.model.push_back(m);
                }
            });
    }
    if (kind == QLatin1String("both") || kind == QLatin1String("tool")) {
        run(QStringLiteral(
                "SELECT id, session_id, turn_id, trace_id, tool_call_id, tool_name, status,"
                " started_at, duration_ms, exit_code, stderr_bytes,"
                " error_type, error_code, error_message"
                " FROM tool_usage WHERE status IN ('error','cancelled') ") + sinceClause +
                QStringLiteral(" ORDER BY started_at DESC LIMIT :limit"),
            binds,
            [&](QSqlQuery& q) {
                while (q.next()) {
                    types::FailedToolRow t;
                    t.id = i64v(q, 0);
                    t.sessionId = sv(q, 1);
                    t.turnId = sv(q, 2);
                    t.traceId = sv(q, 3);
                    t.toolCallId = sv(q, 4);
                    t.toolName = sv(q, 5);
                    t.status = sv(q, 6);
                    t.startedMs = i64v(q, 7);
                    const QVariant dv = q.value(8);
                    if (!dv.isNull()) { t.durationOk = true; t.durationMs = dv.toLongLong(); }
                    const QVariant ev = q.value(9);
                    if (!ev.isNull()) { t.hasExit = true; t.exitCode = ev.toLongLong(); }
                    t.stderrBytes = i64v(q, 10);
                    t.errorType = sv(q, 11);
                    t.errorCode = sv(q, 12);
                    t.errorMessage = sv(q, 13);
                    out.tool.push_back(t);
                }
            });
    }
    return out;
}

QVector<types::SlowToolRow> DbService::slowTools(qint64 sinceMs, int limit)
{
    QVector<types::SlowToolRow> out;
    const QString where = sinceMs >= 0 ? QStringLiteral("WHERE started_at >= :since") : QString();
    const QVariantMap binds = sinceMs >= 0
        ? QVariantMap{{QStringLiteral(":since"), sinceMs}, {QStringLiteral(":limit"), limit}}
        : QVariantMap{{QStringLiteral(":limit"), limit}};
    run(QStringLiteral(
            "SELECT id, session_id, turn_id, trace_id, tool_call_id, tool_name, status,"
            " started_at, duration_ms, exit_code,"
            " substr(COALESCE(error_message,''),1,160) AS err"
            " FROM tool_usage ") + where +
            QStringLiteral(" ORDER BY duration_ms DESC LIMIT :limit"),
        binds,
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::SlowToolRow t;
                t.id = i64v(q, 0);
                t.sessionId = sv(q, 1);
                t.turnId = sv(q, 2);
                t.traceId = sv(q, 3);
                t.toolCallId = sv(q, 4);
                t.toolName = sv(q, 5);
                t.status = sv(q, 6);
                t.startedMs = i64v(q, 7);
                const QVariant dv = q.value(8);
                if (!dv.isNull()) { t.durationOk = true; t.durationMs = dv.toLongLong(); }
                const QVariant ev = q.value(9);
                if (!ev.isNull()) { t.hasExit = true; t.exitCode = ev.toLongLong(); }
                t.err = sv(q, 10);
                out.push_back(t);
            }
        });
    return out;
}

// ───────────────────────── agents forest ─────────────────────────

QVector<types::AgentNode> DbService::agentsForest(const QString& projectId)
{
    struct Flat {
        types::AgentNode node;
    };
    QVector<types::AgentNode> nodes;
    const bool hasProject = !projectId.isEmpty();
    const QString where = hasProject ? QStringLiteral("WHERE project_id = :pid") : QString();
    const QVariantMap binds = hasProject
        ? QVariantMap{{QStringLiteral(":pid"), projectId}} : QVariantMap();

    run(QStringLiteral(
            "SELECT id, title, task_type, parent_id, directory, time_created, time_updated,"
            " (SELECT SUM(computed_total_tokens) FROM model_usage m WHERE m.session_id = s.id) AS tokens,"
            " (SELECT COUNT(*) FROM model_usage m WHERE m.session_id = s.id) AS model_calls,"
            " (SELECT COUNT(*) FROM tool_usage  t WHERE t.session_id = s.id) AS tool_calls"
            " FROM session s ") + where,
        binds,
        [&](QSqlQuery& q) {
            while (q.next()) {
                types::AgentNode n;
                n.id = sv(q, 0);
                n.title = sv(q, 1);
                n.taskType = sv(q, 2);
                n.parentId = sv(q, 3);
                n.directory = sv(q, 4);
                n.timeCreated = i64v(q, 5);
                n.timeUpdated = i64v(q, 6);
                const QVariant tv = q.value(7);
                if (!tv.isNull()) { n.tokensOk = true; n.tokens = tv.toLongLong(); }
                n.modelCalls = i64v(q, 8);
                n.toolCalls = i64v(q, 9);
                nodes.push_back(n);
            }
        });

    // build the forest: attach children to parents, leftovers become roots
    QHash<QString, int> byId;
    for (int i = 0; i < nodes.size(); ++i)
        byId.insert(nodes[i].id, i);
    QVector<types::AgentNode> roots;
    QVector<bool> isChild(nodes.size(), false);
    for (int i = 0; i < nodes.size(); ++i) {
        const QString pid = nodes[i].parentId;
        if (!pid.isEmpty() && byId.contains(pid)) {
            nodes[byId.value(pid)].children.push_back(nodes[i]);
            isChild[i] = true;
        }
    }
    for (int i = 0; i < nodes.size(); ++i) {
        if (!isChild[i])
            roots.push_back(nodes[i]);
    }
    // roots newest first, children chronological
    std::sort(roots.begin(), roots.end(),
              [](const types::AgentNode& a, const types::AgentNode& b) {
                  return a.timeUpdated > b.timeUpdated;
              });
    std::function<void(types::AgentNode&)> sortChildren = [&](types::AgentNode& n) {
        std::sort(n.children.begin(), n.children.end(),
                  [](const types::AgentNode& a, const types::AgentNode& b) {
                      return a.timeCreated < b.timeCreated;
                  });
        for (types::AgentNode& c : n.children)
            sortChildren(c);
    };
    for (types::AgentNode& r : roots)
        sortChildren(r);
    return roots;
}

// ───────────────────────── how page helpers ─────────────────────────

QString DbService::findReasoningSample(qint64* outTimeMs)
{
    QString text;
    run(QStringLiteral(
            "SELECT p.data, p.time_created FROM part p"
            " JOIN message m ON m.id = p.message_id"
            " JOIN session s ON s.id = m.session_id"
            " WHERE s.task_type = 'subagent_child'"
            "   AND json_extract(p.data, '$.type') = 'reasoning'"
            " ORDER BY p.time_created DESC LIMIT 1"),
        {}, [&](QSqlQuery& q) {
            if (!q.next()) return;
            if (outTimeMs)
                *outTimeMs = i64v(q, 1);
            text = js(jobj(sv(q, 0)), "text");
        });
    return text;
}

// per-thread accessors (thread-locals defined above connection management)
QString DbService::lastError() const { return t_lastError; }
bool DbService::lastQueryOk() const { return t_lastOk; }
