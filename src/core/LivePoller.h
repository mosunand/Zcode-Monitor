// LivePoller.h — port of server/routes/live.js (SSE stream) as an in-process
// timer + signals. Polls the DB every 1500ms for model/tool rows committed
// after the rowid watermark and emits them in batches.
//
// Tailing is by ROWID (insert order), not started_at: a long in-flight
// request commits its row LATE, with an started_at that can predate our
// watermark — a timestamp tail misses it forever, a rowid tail cannot.
//
// Watermark init deliberately differs from the original (a bug there): we
// start from MAX(rowid), so only rows committed after app start are pushed
// instead of replaying the whole history.

#include <QObject>
#include <QTimer>

#include "Types.h"

class LivePoller : public QObject {
    Q_OBJECT
public:
    explicit LivePoller(QObject* parent = nullptr);

    void start();
    void stop();
    bool isActive() const { return m_timer.isActive(); }

signals:
    void modelRows(const QVector<types::ModelRow>& rows);
    void toolRows(const QVector<types::ToolRow>& rows);
    void pollError(const QString& message); // transient query failure (badge)
    void recovered();                        // first success after an error

private slots:
    void poll();

private:
    QTimer m_timer;
    qint64 m_lastModelRowId = -1; // -1 = not initialized yet
    qint64 m_lastToolRowId = -1;
    bool m_initDone = false;      // watermarks fetched; polling paused until then
    bool m_hadError = true;       // first successful poll emits recovered() (badge → 已连接)
};
