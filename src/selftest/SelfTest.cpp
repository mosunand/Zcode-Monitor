// SelfTest.cpp — see SelfTest.h.

#include "SelfTest.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>
#include <QStringBuilder>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "core/DbService.h"
#include "core/LivePoller.h"
#include "core/LogTailService.h"
#include "core/Paths.h"
#include "core/RawService.h"
#include "core/RuntimeWatchdog.h"
#include "core/TranscriptService.h"
#include "util/Format.h"

namespace {

int failures = 0;

void check(bool ok, const QString& what)
{
    printf("%s %s\n", ok ? "[OK]  " : "[FAIL]", qPrintable(what));
    if (!ok)
        ++failures;
}

void info(const QString& s)
{
    printf("      %s\n", qPrintable(s));
}

} // namespace

namespace SelfTest {

int run()
{
    printf("== zcode-monitor selftest ==\n");
    printf("db path:   %s\n", qPrintable(Paths::dbPath()));
    printf("log dir:   %s\n", qPrintable(Paths::logDir()));
    printf("agents:    %s\n", qPrintable(Paths::agentsDir()));
    printf("exec:      %s\n\n", qPrintable(Paths::execDir()));

    auto& db = DbService::instance();

    // ── connection ──
    QString warmErr;
    check(db.warm(&warmErr), QStringLiteral("warm db (err=%1)").arg(warmErr));
    check(db.probe(), "probe SELECT 1");

    // ── overview ──
    const types::OverviewData ov = db.overview(QStringLiteral("24h"));
    const types::Kpis& k = ov.kpis;
    info(QStringLiteral("kpis: calls=%1 completed=%2 errors=%3 cancelled=%4 inTok=%5 outTok=%6 "
                        "reasonTok=%7 cacheRead=%8 avgDur=%9")
             .arg(k.calls).arg(k.completed).arg(k.errors).arg(k.cancelled)
             .arg(k.inTok).arg(k.outTok).arg(k.reasonTok).arg(k.cacheRead)
             .arg(k.avgDurationOk ? QString::number(k.avgDurationMs) : "null"));
    info(QStringLiteral("series points=%1 byModel=%2 byTool=%3 speedTps=%4 recentSpeed=%5")
             .arg(ov.series.size()).arg(ov.byModel.size()).arg(ov.byTool.size())
             .arg(ov.speed.weightedOk ? QString::number(ov.speed.weightedTps) : "null")
             .arg(ov.recentSpeed.size()));
    check(k.calls > 0, "overview has model calls in 24h");
    check(!ov.series.isEmpty() || k.calls == 0, "series query ran");
    const types::OverviewData ovToday = db.overview(QStringLiteral("today"));
    info(QStringLiteral("today: calls=%1 activeSessions=%2").arg(ovToday.kpis.calls)
             .arg(ovToday.kpis.activeSessions));
    const types::OverviewData ov7 = db.overview(QStringLiteral("7d"));
    info(QStringLiteral("7d: calls=%1 series=%2").arg(ov7.kpis.calls).arg(ov7.series.size()));

    // ── live watermarks ──
    const qint64 wmM = db.latestModelStartedAt();
    const qint64 wmT = db.latestToolStartedAt();
    info(QStringLiteral("watermarks: model=%1 tool=%2").arg(wmM).arg(wmT));
    check(wmM > 0, "latest model started_at found");
    const QVector<types::ModelRow> recent = db.recentModelRows(0, 3);
    info(QStringLiteral("recentModelRows(0,3) → %1 rows, first started=%2")
             .arg(recent.size())
             .arg(recent.isEmpty() ? QStringLiteral("-")
                                   : QDateTime::fromMSecsSinceEpoch(recent.first().startedMs)
                                         .toString(Qt::ISODate)));

    // ── sessions ──
    const QVector<types::SessionRow> sessions = db.sessionList(500, 0, QString(), QString());
    check(!sessions.isEmpty(), "session list not empty");
    info(QStringLiteral("sessions: %1, first=%2 (%3)")
             .arg(sessions.size())
             .arg(sessions.isEmpty() ? QStringLiteral("-") : sessions.first().id.left(24))
             .arg(sessions.isEmpty() ? QStringLiteral("-") : sessions.first().taskType));

    const QVector<types::SessionRow> subagents = db.sessionList(200, 0, QString(),
                                                                QStringLiteral("subagent_child"));
    info(QStringLiteral("subagent sessions: %1").arg(subagents.size()));
    check(!subagents.isEmpty(), "subagent sessions found");

    if (!sessions.isEmpty()) {
        const types::SessionDetail detail = db.sessionGet(sessions.first().id);
        check(detail.found, "sessionGet found");
        info(QStringLiteral("detail: id=%1 title=%2 dir=%3")
                 .arg(detail.id.left(20))
                 .arg(detail.title.left(40))
                 .arg(detail.directory));

        const QVector<types::TurnRow> turns = db.sessionTurns(sessions.first().id);
        info(QStringLiteral("turns for first session: %1").arg(turns.size()));

        const QVector<types::Message> msgs =
            db.sessionConversation(sessions.first().id, 800);
        check(!msgs.isEmpty(), "conversation has messages");
        info(QStringLiteral("conversation: %1 messages, first role=%2 parts=%3")
                 .arg(msgs.size())
                 .arg(msgs.isEmpty() ? QStringLiteral("-") : msgs.first().role)
                 .arg(msgs.isEmpty() ? 0 : msgs.first().parts.size()));

        const QVector<types::ActivityRow> act = db.sessionActivity(sessions.first().id, 200);
        info(QStringLiteral("activity: %1 rows").arg(act.size()));

        const QVector<types::ChildAgent> kids = db.sessionChildrenEnriched(sessions.first().id);
        info(QStringLiteral("children of first session: %1").arg(kids.size()));

        const QVector<types::TodoRow> todos = db.todosForSession(sessions.first().id);
        info(QStringLiteral("todos: %1").arg(todos.size()));
    }

    // ── transcript (timeline tab data source) ──
    if (!subagents.isEmpty()) {
        // some agents have metadata but an empty/absent transcript.jsonl —
        // walk a few until we find one with events.
        types::TranscriptData td;
        QString sid;
        for (const types::SessionRow& s : subagents) {
            td = TranscriptService::readTranscript(s.id, 4000);
            sid = s.id;
            if (td.found && td.count > 0)
                break;
            if (!td.found)
                break; // locateAgent itself failed — report that
        }
        check(td.found, QStringLiteral("transcript located for %1…").arg(sid.left(30)));
        info(QStringLiteral("transcript: count=%1 shown=%2 byType=%3 tools=%4 tokens in/out/cache=%5/%6/%7")
                 .arg(td.count).arg(td.events.size()).arg(td.aggregate.byType.size())
                 .arg(td.aggregate.tools.size())
                 .arg(td.aggregate.tokensInput)
                 .arg(td.aggregate.tokensOutput)
                 .arg(td.aggregate.tokensCache));
        if (!td.events.isEmpty()) {
            const types::TranscriptEvent& e = td.events.first();
            info(QStringLiteral("first event: type=%1 cat=%2 label=%3 summary=%4")
                     .arg(e.type).arg(e.category).arg(e.label)
                     .arg(e.summary.left(70)));
        }
        // verify limit slicing
        const types::TranscriptData limited = TranscriptService::readTranscript(sid, 5);
        check(limited.events.size() <= 5, "transcript limit slicing");
    } else {
        // fall back: locate any agent dir
        check(false, "no subagent session to test transcript with");
    }

    // ── errors & trace ──
    const types::ErrorSummary es = db.errorSummary(-1);
    info(QStringLiteral("errorSummary(all): modelTypes=%1 byToolName=%2 byToolErr=%3")
             .arg(es.byModelErrorType.size()).arg(es.byToolName.size())
             .arg(es.byToolErrorType.size()));
    const types::FailedItems fi = db.errorsList(-1, QStringLiteral("both"), 200);
    info(QStringLiteral("failed: model=%1 tool=%2").arg(fi.model.size()).arg(fi.tool.size()));
    const QVector<types::SlowToolRow> slow = db.slowTools(-1, 30);
    info(QStringLiteral("slowTools: %1").arg(slow.size()));

    QString traceId;
    if (!fi.model.isEmpty())
        traceId = fi.model.first().traceId;
    if (!traceId.isEmpty()) {
        const QVector<types::LogEvent> evs = LogTailService::eventsForTrace(traceId);
        info(QStringLiteral("eventsForTrace(%1…): %2 events")
                 .arg(traceId.left(12)).arg(evs.size()));
        const QVector<types::SpanNode> forest = LogTailService::buildSpanForest(evs);
        info(QStringLiteral("span forest roots: %1").arg(forest.size()));
    }
    const QVector<types::LogEvent> tail = LogTailService::tailLog(10);
    info(QStringLiteral("log tail: %1 events").arg(tail.size()));

    // ── agents forest ──
    const QVector<types::AgentNode> forest = db.agentsForest();
    check(!forest.isEmpty(), "agents forest built");
    int totalNodes = 0;
    std::function<int(const QVector<types::AgentNode>&)> countNodes =
        [&](const QVector<types::AgentNode>& nodes) -> int {
        int n = nodes.size();
        for (const types::AgentNode& x : nodes)
            n += countNodes(x.children);
        return n;
    };
    totalNodes = countNodes(forest);
    info(QStringLiteral("forest: %1 roots, %2 total sessions").arg(forest.size()).arg(totalNodes));

    // ── raw viewer ──
    const types::RawResult raw = RawService::query(QStringLiteral("session"), 3, 0,
                                                   QStringLiteral("time_updated"), true, QString());
    check(raw.ok && raw.rows.size() > 0, "raw session query");
    info(QStringLiteral("raw: count=%1 rows=%2 cols=%3")
             .arg(raw.count).arg(raw.rows.size()).arg(raw.columns.join(',')));
    const types::RawResult rawBad = RawService::query(QStringLiteral("sqlite_master"), 3);
    check(!rawBad.error.isEmpty(), "raw rejects non-allowlisted table");
    const types::RawResult rawWhere = RawService::query(
        QStringLiteral("model_usage"), 3, 0, QStringLiteral("started_at"), true,
        QStringLiteral("status='error'"));
    info(QStringLiteral("raw where status=error: rows=%1 count=%2 ok=%3")
             .arg(rawWhere.rows.size()).arg(rawWhere.count).arg(rawWhere.ok));

    // ── reasoning ──
    if (!subagents.isEmpty()) {
        const QVector<types::ReasoningItem> reasoning =
            db.sessionReasoning(subagents.first().id, 1);
        info(QStringLiteral("reasoning(first subagent): %1 items")
                 .arg(reasoning.size()));
    }
    const QString sample = db.findReasoningSample();
    info(QStringLiteral("reasoning sample: %1 chars").arg(sample.size()));

    // ── runtime watchdog ──
    const types::WalStatus wal = RuntimeWatchdog::walStatus(Paths::dbPath());
    check(wal.valid, "walStatus readable");
    info(QStringLiteral("wal: main=%1 wal=%2 shm=%3 zcodeRunning=%4")
             .arg(wal.mainBytes).arg(wal.walBytes).arg(wal.shmBytes)
             .arg(RuntimeWatchdog::isZCodeProcessRunning() ? "yes" : "no"));

    // ── checkpoint end-to-end (on a throwaway WAL db in the temp dir) ──
    // This exercises the exact code path behind the topbar button and the
    // exit-triggered auto-checkpoint, with zero risk to the real database.
    // The writer connection stays open across the checkpoint call — closing
    // it first would let SQLite auto-checkpoint and delete the -wal file,
    // which would make the "WAL had pending frames" assertion meaningless.
    {
        const QString tmpDb = QDir::temp().filePath(
            QStringLiteral("zcode-monitor-selftest-%1.db").arg(QDateTime::currentMSecsSinceEpoch()));
        QSqlDatabase writer = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                        QStringLiteral("zcselftest"));
        writer.setDatabaseName(tmpDb);
        bool made = false;
        if (writer.open()) {
            QSqlQuery q(writer);
            made = q.exec(QStringLiteral("PRAGMA journal_mode = WAL"))
                && q.exec(QStringLiteral("CREATE TABLE t(x INTEGER)"))
                && q.exec(QStringLiteral("INSERT INTO t VALUES (42)"));
        }
        check(made, "checkpoint test: temp WAL db created");

        const types::WalStatus before = RuntimeWatchdog::walStatus(tmpDb);
        const types::CheckpointResult cp = RuntimeWatchdog::checkpointNow(tmpDb);
        const types::WalStatus after = RuntimeWatchdog::walStatus(tmpDb);
        check(cp.ok, QStringLiteral("checkpoint test: ok (err=%1)").arg(cp.error));
        check(before.walBytes > 0,
              QStringLiteral("checkpoint test: WAL had pending frames (got %1)").arg(before.walBytes));
        check(after.walBytes < before.walBytes,
              QStringLiteral("checkpoint test: WAL folded (was %1, now %2)")
                  .arg(before.walBytes).arg(after.walBytes));
        writer.close();
        QSqlDatabase::removeDatabase(QStringLiteral("zcselftest"));
        // data must still be readable after the fold
        {
            QSqlDatabase t = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("zcselftest2"));
            t.setDatabaseName(tmpDb);
            t.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            QString value;
            if (t.open()) {
                QSqlQuery q(t);
                if (q.exec(QStringLiteral("SELECT x FROM t")) && q.next())
                    value = q.value(0).toString();
                t.close();
            }
            QSqlDatabase::removeDatabase(QStringLiteral("zcselftest2"));
            check(value == QLatin1String("42"), "checkpoint test: data intact after fold");
        }
        QFile::remove(tmpDb);
        QFile::remove(tmpDb + QStringLiteral("-wal"));
        QFile::remove(tmpDb + QStringLiteral("-shm"));
        info(QStringLiteral("checkpoint test: wal %1 → %2 bytes, checkpointed=%3")
                 .arg(before.walBytes).arg(after.walBytes).arg(cp.checkpointed));
    }

    // ── format helpers ──
    check(Format::fmtNum(1234.0) == QStringLiteral("1.2k"), "fmtNum k");
    check(Format::fmtNum(1234567.0) == QStringLiteral("1.23M"), "fmtNum M");
    check(Format::fmtMs(950) == QStringLiteral("950ms"), "fmtMs");
    check(Format::fmtMs(9500) == QStringLiteral("9.5s"), "fmtMs s");
    check(Format::fmtDur(125) == QStringLiteral("2m5s"), "fmtDur");
    check(Format::speedTier(29) == 1 && Format::speedTier(30) == 2
              && Format::speedTier(81) == 3, "speedTier boundaries");

    printf("\n== %s (%d failures) ==\n",
           failures == 0 ? "SELFTEST PASSED" : "SELFTEST FAILED", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace SelfTest
