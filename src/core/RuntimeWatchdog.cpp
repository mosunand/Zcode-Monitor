// RuntimeWatchdog.cpp — see RuntimeWatchdog.h.

#include "RuntimeWatchdog.h"

#include <QAtomicInt>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "DbService.h"
#include "Paths.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

// ── process detection ──────────────────────────────────────────
// Matches the ZCode app binary (ZCode.exe / ZCode), the CLI helper
// (zcode-cli) and the local host process (zcode-host-local). On non-Windows
// we stay optimistic (true), exactly like the original's ps fallback.

bool RuntimeWatchdog::isZCodeProcessRunning()
{
#ifdef Q_OS_WIN
    // compare lowercase exe names
    static const QStringList names = {
        QStringLiteral("zcode.exe"),     QStringLiteral("zcode"),
        QStringLiteral("zcode-cli.exe"), QStringLiteral("zcode-cli"),
        QStringLiteral("zcode-host-local.exe"), QStringLiteral("zcode-host-local"),
    };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return true; // can't tell → optimistic (same as the original's catch)
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    BOOL ok = Process32FirstW(snap, &pe);
    bool found = false;
    while (ok) {
        const QString exe = QString::fromWCharArray(pe.szExeFile).toLower();
        if (names.contains(exe)) {
            found = true;
            break;
        }
        ok = Process32NextW(snap, &pe);
    }
    CloseHandle(snap);
    return found;
#else
    // Optimistic fallback: assume running so we stay in safe read-only mode
    // rather than risk a contended checkpoint.
    return true;
#endif
}

types::WalStatus RuntimeWatchdog::walStatus(const QString& dbPath)
{
    types::WalStatus w;
    const QFileInfo main(dbPath);
    if (!main.exists())
        return w;
    w.valid = true;
    w.mainBytes = main.size();
    const QFileInfo wal(dbPath + QStringLiteral("-wal"));
    if (wal.exists())
        w.walBytes = wal.size();
    const QFileInfo shm(dbPath + QStringLiteral("-shm"));
    if (shm.exists())
        w.shmBytes = shm.size();
    return w;
}

types::CheckpointResult RuntimeWatchdog::checkpointNow(const QString& dbPath)
{
    types::CheckpointResult r;
    r.before = walStatus(dbPath);

    // transient writable connection (the only write access in the whole app).
    // Unique connection name: a second call while the first still runs (watchdog
    // poll racing the manual button) must never collide with a closing handle.
    static QAtomicInt seq = 1;
    const QString connName = QStringLiteral("zccheckpoint-%1").arg(seq.fetchAndAddRelaxed(1));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=10000"));
        if (!db.open()) {
            r.error = db.lastError().text();
        } else {
            {
                QSqlQuery q(db);
                q.exec(QStringLiteral("PRAGMA busy_timeout = 10000"));
                if (q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"))) {
                    if (q.next()) {
                        r.busy = q.value(0).toInt();
                        r.checkpointed = q.value(2).toInt();
                    }
                    r.ok = true;
                } else {
                    r.error = q.lastError().text();
                }
            }
            db.close();
        }
        db = QSqlDatabase(); // release the handle before removeDatabase
    }
    QSqlDatabase::removeDatabase(connName);
    r.after = walStatus(dbPath);
    return r;
}

// ── watchdog ───────────────────────────────────────────────────

RuntimeWatchdog::RuntimeWatchdog(QObject* parent)
    : QObject(parent)
{
    // initial poll (no checkpoint at boot — ZCode may already be down, fine)
    m_running = isZCodeProcessRunning();
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &RuntimeWatchdog::poll);
    m_timer.start();
}

void RuntimeWatchdog::poll()
{
    const bool running = isZCodeProcessRunning();
    const bool wasRunning = m_running;
    m_running = running;
    if (wasRunning != running)
        emit zcodeRunningChanged(running);

    // Transition running → stopped: fold the WAL so history stays readable.
    if (wasRunning && !running) {
        const types::WalStatus before = walStatus(Paths::dbPath());
        const types::CheckpointResult result = checkpointNow(Paths::dbPath());
        applyCheckpointRecord(result, true);
        Q_UNUSED(before);
        // Drop the read-only connection cache so the next read sees the folded db.
        DbService::instance().invalidate();
    }
}

void RuntimeWatchdog::applyCheckpointRecord(const types::CheckpointResult& r, bool autoTriggered)
{
    types::CheckpointRecord rec;
    rec.valid = true;
    rec.atMs = QDateTime::currentMSecsSinceEpoch();
    rec.ok = r.ok;
    if (r.ok) {
        rec.walBefore = r.before.walBytes;
        rec.walAfter = r.after.walBytes;
        if (r.before.valid && r.after.valid) {
            rec.foldedOk = true;
            rec.folded = r.before.walBytes - r.after.walBytes;
        }
    } else {
        rec.error = r.error;
    }
    m_lastCheckpoint = rec;
    emit checkpointDone(rec);
}

types::CheckpointResult RuntimeWatchdog::requestCheckpoint(bool force)
{
    if (m_running && !force) {
        types::CheckpointResult r;
        r.ok = false;
        r.error = QStringLiteral("zcode_running");
        return r;
    }
    const types::WalStatus before = walStatus(Paths::dbPath());
    const types::CheckpointResult result = checkpointNow(Paths::dbPath());
    if (result.ok) {
        // close this (pool) thread's cached connection; the GUI thread's
        // read-only connection survives checkpoints safely and self-heals
        DbService::instance().invalidate();
        // m_lastCheckpoint + the signal land on the GUI thread via the queued
        // connection in poll(); the button path only uses the return value,
        // so no cross-thread state writes happen here
        types::CheckpointRecord rec;
        rec.valid = true;
        rec.atMs = QDateTime::currentMSecsSinceEpoch();
        rec.ok = true;
        rec.walBefore = before.valid ? before.walBytes : 0;
        rec.walAfter = result.after.valid ? result.after.walBytes : 0;
        emit checkpointDone(rec);
    }
    return result;
}
