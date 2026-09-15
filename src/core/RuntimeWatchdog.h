#pragma once
// RuntimeWatchdog.h — port of server/zcode-runtime.js + the watchdog in server/index.js.
// Polls every 5s whether ZCode is running; on the running→stopped edge, folds
// the WAL into the main db (wal_checkpoint TRUNCATE) so read-only connections
// keep seeing complete history after ZCode exits. We never write otherwise.
//
// Windows process detection uses Toolhelp32Snapshot natively — the original
// shelled out to `ps -axo comm`, which never exists on Windows and silently
// reported "running" forever (auto-checkpoint never fired). Fixed here.

#include <QObject>
#include <QTimer>

#include "Types.h"

class RuntimeWatchdog : public QObject {
    Q_OBJECT
public:
    explicit RuntimeWatchdog(QObject* parent = nullptr);

    bool isZcodeRunning() const { return m_running; }
    types::CheckpointRecord lastCheckpoint() const { return m_lastCheckpoint; }
    // GUI-thread sink for queued checkpointDone signals (thread-safe record)
    void recordCheckpoint(const types::CheckpointRecord& rec) { m_lastCheckpoint = rec; }

    // manual checkpoint (topbar button). force = web ?force=1.
    // Returns the result; on refusal by a running ZCode, ok=false & error="zcode_running".
    types::CheckpointResult requestCheckpoint(bool force);

    // static helpers (port of zcode-runtime.js)
    static bool isZCodeProcessRunning();
    static types::WalStatus walStatus(const QString& dbPath);
    static types::CheckpointResult checkpointNow(const QString& dbPath);

signals:
    void zcodeRunningChanged(bool running);
    void checkpointDone(const types::CheckpointRecord& record);

public slots:
    void poll();

private:
    void applyCheckpointRecord(const types::CheckpointResult& r, bool autoTriggered);

    QTimer m_timer;
    bool m_running = true;
    types::CheckpointRecord m_lastCheckpoint;
};
