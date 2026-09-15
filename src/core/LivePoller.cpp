// LivePoller.cpp — see LivePoller.h.

#include "LivePoller.h"

#include "DbService.h"
#include "util/Async.h"

LivePoller::LivePoller(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(1500);
    connect(&m_timer, &QTimer::timeout, this, &LivePoller::poll);
}

void LivePoller::start()
{
    // watermark init off-thread; polling stays paused (m_initDone false)
    // until it lands, so the feed never replays history
    Async::run<QPair<qint64, qint64>>(
        this,
        []() {
            return QPair<qint64, qint64>(DbService::instance().latestModelRowId(),
                                         DbService::instance().latestToolRowId());
        },
        [this](const QPair<qint64, qint64>& v) {
            m_lastModelRowId = v.first;
            m_lastToolRowId = v.second;
            m_initDone = true;
        });
    m_timer.start();
}

void LivePoller::stop()
{
    m_timer.stop();
}

void LivePoller::poll()
{
    if (!m_initDone)
        return; // watermark init still in flight

    struct PollResult {
        QVector<types::ModelRow> models;
        QVector<types::ToolRow> tools;
        qint64 maxModelRid = 0, maxToolRid = 0;
        bool ok = false;
        QString err;
    };
    const qint64 modelRid = m_lastModelRowId;
    const qint64 toolRid = m_lastToolRowId;

    // queries run on a pool thread — the GUI thread must never touch SQLite
    // (a contended read can sleep in busy-retry for hundreds of ms, which
    // freezes the window and Windows flags it "not responding")
    Async::run<PollResult>(
        this,
        [modelRid, toolRid]() {
            auto& db = DbService::instance();
            PollResult r;
            r.models = db.modelRowsAfterRowid(modelRid, 100);
            const bool modelsOk = db.lastQueryOk();
            const QString modelsErr = db.lastError();
            r.tools = db.toolRowsAfterRowid(toolRid, 100);
            const bool toolsOk = db.lastQueryOk();
            const QString toolsErr = db.lastError();
            r.ok = modelsOk && toolsOk;
            r.err = modelsOk ? toolsErr : modelsErr;
            for (const auto& m : r.models)
                r.maxModelRid = qMax(r.maxModelRid, m.tailRid);
            for (const auto& t : r.tools)
                r.maxToolRid = qMax(r.maxToolRid, t.tailRid);
            return r;
        },
        [this](const PollResult& r) {
            if (!r.ok) {
                m_hadError = true;
                emit pollError(r.err);
                return;
            }
            if (m_hadError) {
                m_hadError = false;
                emit recovered(); // flips the live badge to 已连接
            }
            if (r.models.isEmpty() && r.tools.isEmpty())
                return; // nothing new

            m_lastModelRowId = qMax(m_lastModelRowId, r.maxModelRid);
            m_lastToolRowId = qMax(m_lastToolRowId, r.maxToolRid);

            if (!r.models.isEmpty())
                emit modelRows(r.models);
            if (!r.tools.isEmpty())
                emit toolRows(r.tools);
        });
}
