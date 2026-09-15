// LogTailService.cpp — see LogTailService.h.

#include "LogTailService.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <functional>

#include "Paths.h"

namespace {

QString js(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : QString();
}

} // namespace

QString LogTailService::todayLogFile()
{
    return logFileFor(QDateTime::currentDateTimeUtc().date());
}

QString LogTailService::logFileFor(const QDate& utcDate)
{
    return Paths::logDir() + QStringLiteral("/zcode-") + utcDate.toString(Qt::ISODate)
         + QStringLiteral(".jsonl");
}

types::LogEvent LogTailService::parseLine(const QString& line)
{
    types::LogEvent e;
    if (line.trimmed().isEmpty())
        return e;
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject())
        return e; // invalid → caller checks raw.isEmpty()
    e.raw = d.object();
    e.timestampRaw = js(e.raw, "timestamp");
    if (e.raw.contains(QLatin1String("timestamp"))) {
        const QJsonValue v = e.raw.value(QLatin1String("timestamp"));
        if (v.isString()) {
            QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
            if (!dt.isValid())
                dt = QDateTime::fromString(v.toString(), Qt::ISODate);
            if (dt.isValid()) {
                e.tsOk = true;
                e.timestampMs = dt.toMSecsSinceEpoch();
            }
        } else if (v.isDouble()) {
            e.tsOk = true;
            e.timestampMs = qint64(v.toDouble());
        }
    }
    e.level = js(e.raw, "level");
    e.event = js(e.raw, "event");
    e.module = js(e.raw, "module");
    e.message = js(e.raw, "message");
    e.traceId = js(e.raw, "traceId");
    e.spanId = js(e.raw, "spanId");
    e.parentSpanId = js(e.raw, "parentSpanId");
    e.sessionId = js(e.raw, "sessionId");
    e.turnId = js(e.raw, "turnId");
    e.status = js(e.raw, "status");
    const QJsonValue dur = e.raw.value(QLatin1String("durationMs"));
    if (dur.isDouble()) {
        e.durationOk = true;
        e.durationMs = qint64(dur.toDouble());
    }
    return e;
}

QVector<types::LogEvent> LogTailService::tailLog(int lines)
{
    QVector<types::LogEvent> out;
    const QString file = todayLogFile();
    QFileInfo info(file);
    if (!info.exists() || info.size() == 0)
        return out;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return out;

    // read the last chunk (up to 1MB) and split lines
    const qint64 chunkSize = qMin(info.size(), qint64(1024 * 1024));
    f.seek(info.size() - chunkSize);
    const QString text = QString::fromUtf8(f.read(chunkSize));

    QStringList all = text.split(QLatin1Char('\n'));
    if (info.size() > chunkSize && !all.isEmpty())
        all.removeFirst(); // first line is cut mid-way — drop it

    // collect the newest N valid lines, then restore chronological order
    for (int i = all.size() - 1; i >= 0 && out.size() < lines; --i) {
        const types::LogEvent e = parseLine(all.at(i));
        if (!e.raw.isEmpty())
            out.push_back(e);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

QVector<types::LogEvent> LogTailService::eventsForTrace(const QString& traceId)
{
    QVector<types::LogEvent> out;
    if (traceId.isEmpty())
        return out;
    // today + yesterday (UTC day boundaries shift)
    const QDate today = QDateTime::currentDateTimeUtc().date();
    const QStringList files = { logFileFor(today), logFileFor(today.addDays(-1)) };
    for (const QString& file : files) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        while (!f.atEnd()) {
            const types::LogEvent e = parseLine(QString::fromUtf8(f.readLine()));
            if (!e.raw.isEmpty() && e.traceId == traceId)
                out.push_back(e);
        }
    }
    // sort by timestamp (equivalent to the ISO localeCompare in the original)
    std::sort(out.begin(), out.end(),
              [](const types::LogEvent& a, const types::LogEvent& b) {
                  return a.timestampRaw < b.timestampRaw;
              });
    return out;
}

QVector<types::SpanNode> LogTailService::buildSpanForest(const QVector<types::LogEvent>& events)
{
    struct Node {
        int eventIndex = -1;
        QVector<int> children;
    };
    QHash<QString, int> bySpan; // key → node index
    QVector<Node> nodes;
    const auto keyFor = [&events](int i) -> QString {
        const types::LogEvent& e = events.at(i);
        if (!e.spanId.isEmpty())
            return e.spanId;
        return QStringLiteral("ev_%1").arg(i); // stable per index (fixes the O(n²) indexOf)
    };
    for (int i = 0; i < events.size(); ++i) {
        const QString key = keyFor(i);
        if (!bySpan.contains(key)) {
            bySpan.insert(key, nodes.size());
            nodes.push_back(Node{i, {}});
        }
    }

    // Attach each node to its parent at most once. The original pushed one
    // entry per EVENT, so spans appearing in many events (start/end pairs with
    // the same parent) were duplicated at every level — with deep chains that
    // explodes exponentially when the tree is materialized. Duplicates and
    // self/mutual cycles are dropped here instead.
    QVector<bool> asChild(nodes.size(), false);
    for (int i = 0; i < events.size(); ++i) {
        const types::LogEvent& e = events.at(i);
        if (e.parentSpanId.isEmpty())
            continue;
        const auto it = bySpan.constFind(e.parentSpanId);
        if (it == bySpan.constEnd())
            continue;
        const int nodeIdx = bySpan.value(keyFor(i));
        const int parentIdx = it.value();
        if (parentIdx == nodeIdx || asChild.at(nodeIdx))
            continue; // self-parent → stays a root; already attached elsewhere
        nodes[parentIdx].children.push_back(nodeIdx);
        asChild[nodeIdx] = true;
    }
    QVector<int> roots;
    for (int i = 0; i < nodes.size(); ++i) {
        if (!asChild.at(i))
            roots.push_back(i);
    }

    // materialize the forest (nodes reference events by index)
    std::function<void(int, types::SpanNode&)> build = [&](int idx, types::SpanNode& out) {
        out.event = events.at(nodes.at(idx).eventIndex);
        for (int child : nodes.at(idx).children) {
            types::SpanNode c;
            build(child, c);
            out.children.push_back(c);
        }
    };
    QVector<types::SpanNode> forest;
    forest.reserve(roots.size());
    for (int r : roots) {
        types::SpanNode n;
        build(r, n);
        forest.push_back(n);
    }
    return forest;
}
