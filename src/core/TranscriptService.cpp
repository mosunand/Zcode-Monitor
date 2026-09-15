// TranscriptService.cpp — see TranscriptService.h.

#include "TranscriptService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

#include <cmath>

#include "Paths.h"

namespace {

QJsonObject parseJsonObjectFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    return d.isObject() ? d.object() : QJsonObject();
}

QString js(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : QString();
}

qint64 jms(const QJsonObject& o, const char* key, bool* ok)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isString()) {
        QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(v.toString(), Qt::ISODate);
        if (dt.isValid()) {
            if (ok) *ok = true;
            return dt.toMSecsSinceEpoch();
        }
    } else if (v.isDouble()) {
        if (ok) *ok = true;
        return qint64(v.toDouble());
    }
    if (ok) *ok = false;
    return 0;
}

} // namespace

QString TranscriptService::agentUuidFromChild(const QString& childSessionId)
{
    if (childSessionId.isEmpty())
        return {};
    static const QRegularExpression re(
        QStringLiteral("agent_([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})$"));
    const QRegularExpressionMatch m = re.match(childSessionId);
    return m.hasMatch() ? m.captured(1) : QString();
}

TranscriptService::AgentLocation TranscriptService::locateAgent(const QString& childSessionId)
{
    AgentLocation loc;
    const QString uuid = agentUuidFromChild(childSessionId);
    if (uuid.isEmpty())
        return loc;
    const QString agentDirName = QStringLiteral("agent_") + uuid;
    const QDir agentsDir(Paths::agentsDir());
    if (!agentsDir.exists())
        return loc;
    const QStringList parents = agentsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& parentSess : parents) {
        const QString candidate = agentsDir.filePath(parentSess + QLatin1Char('/') + agentDirName);
        const QString metaPath = candidate + QStringLiteral("/metadata.json");
        if (QFile::exists(metaPath)) {
            const QJsonObject meta = parseJsonObjectFile(metaPath);
            if (!meta.isEmpty()) {
                loc.found = true;
                loc.parentSessionId = parentSess;
                loc.agentId = agentDirName;
                loc.dir = candidate;
                loc.meta = meta;
                return loc;
            }
        }
    }
    return loc;
}

types::TranscriptData TranscriptService::readTranscript(const QString& childSessionId,
                                                        int limit,
                                                        const QStringList& typeFilter)
{
    types::TranscriptData out;
    const AgentLocation loc = locateAgent(childSessionId);
    if (!loc.found) {
        out.found = false;
        out.message = QStringLiteral(
            "该会话没有 transcript.jsonl（主交互会话只有 message/part 表，没有事件流；切到 Context 看完整对话）。");
        return out;
    }

    // meta
    {
        const QJsonObject& m = loc.meta;
        types::TranscriptMeta& meta = out.meta;
        meta.valid = true;
        meta.raw = m;
        meta.agentId = js(m, "agentId");
        meta.profileId = js(m, "profileId");
        meta.childSessionId = js(m, "childSessionId");
        meta.parentSessionId = js(m, "parentSessionId");
        meta.parentToolUseId = js(m, "parentToolUseId");
        meta.cwd = js(m, "cwd");
        meta.description = js(m, "description");
        meta.status = js(m, "status");
        meta.createdAtMs = jms(m, "createdAt", &meta.createdAtOk);
        meta.completedAtMs = jms(m, "completedAt", &meta.completedAtOk);
        meta.totalDurationMs = jms(m, "totalDurationMs", &meta.totalDurationOk);
        meta.totalTokens = jms(m, "totalTokens", &meta.totalTokensOk);
        meta.totalToolUseCount = jms(m, "totalToolUseCount", &meta.totalToolUseOk);
        if (m.contains(QLatin1String("usage")))
            meta.usage = m.value(QLatin1String("usage")).toObject();
    }

    out.found = true;

    const QString tfile = loc.dir + QStringLiteral("/transcript.jsonl");
    QFile f(tfile);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return out;

    QVector<types::TranscriptEvent> all;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty())
            continue;
        QJsonParseError err;
        const QJsonDocument d = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !d.isObject())
            continue; // bad line → skip silently
        const QJsonObject o = d.object();
        const QString type = js(o, "type");
        if (!typeFilter.isEmpty() && !typeFilter.contains(type))
            continue;
        types::TranscriptEvent ev;
        const QJsonValue seq = o.value(QLatin1String("sequenceNumber"));
        if (seq.isDouble()) { ev.seqOk = true; ev.sequenceNumber = qint64(seq.toDouble()); }
        ev.type = type;
        ev.timestampRaw = js(o, "timestamp");
        ev.timestampMs = jms(o, "timestamp", &ev.tsOk);
        ev.turnId = js(o, "turnId");
        ev.traceId = js(o, "traceId");
        ev.sessionId = js(o, "sessionId");
        if (o.contains(QLatin1String("payload")))
            ev.payload = o.value(QLatin1String("payload")).toObject();
        categorize(ev);
        ev.summary = summarize(ev);
        all.push_back(ev);
    }
    out.count = all.size();
    out.events = limit > 0 ? all.mid(0, limit) : all;
    out.aggregate = aggregate(all);
    return out;
}

void TranscriptService::categorize(types::TranscriptEvent& ev)
{
    const QString& t = ev.type;
    const QJsonObject& p = ev.payload;
    if (t == QLatin1String("turn_started")) {
        ev.category = QStringLiteral("prompt");     ev.label = QStringLiteral("prompt");    ev.icon = QStringLiteral("▸");
    } else if (t == QLatin1String("turn_complete")) {
        ev.category = QStringLiteral("usage");      ev.label = QStringLiteral("turn.end"); ev.icon = QStringLiteral("◾");
    } else if (t == QLatin1String("model_request")) {
        ev.category = QStringLiteral("llm");         ev.label = QStringLiteral("llm→");      ev.icon = QStringLiteral("↗");
    } else if (t == QLatin1String("model_complete")) {
        ev.category = QStringLiteral("usage");      ev.label = QStringLiteral("usage");     ev.icon = QStringLiteral("◼");
    } else if (t == QLatin1String("model_network_status")) {
        ev.category = QStringLiteral("network");     ev.label = QStringLiteral("network");   ev.icon = QStringLiteral("⇄");
    } else if (t == QLatin1String("model_streaming")) {
        ev.category = QStringLiteral("llm");         ev.label = QStringLiteral("stream");    ev.icon = QStringLiteral("~");
    } else if (t == QLatin1String("streaming_tool_ledger_updated")) {
        const QString status = js(p, "status");
        if (status == QLatin1String("tool_result_committed")) {
            ev.category = QStringLiteral("tool"); ev.label = QStringLiteral("tool.result"); ev.icon = QStringLiteral("↓");
        } else {
            ev.category = QStringLiteral("tool"); ev.label = QStringLiteral("tool.call");   ev.icon = QStringLiteral("↑");
        }
    } else if (t == QLatin1String("tool_call_scheduled")) {
        ev.category = QStringLiteral("tool");        ev.label = QStringLiteral("tool.sched"); ev.icon = QStringLiteral("⋔");
    } else if (t == QLatin1String("tool_batch_complete")) {
        ev.category = QStringLiteral("tool");        ev.label = QStringLiteral("tool.batch"); ev.icon = QStringLiteral("◇");
    } else if (t == QLatin1String("stream_recovery_anchor_created")) {
        ev.category = QStringLiteral("lifecycle");   ev.label = QStringLiteral("recovery");   ev.icon = QStringLiteral("⟲");
    } else if (t == QLatin1String("checkpoint_created")) {
        ev.category = QStringLiteral("lifecycle");   ev.label = QStringLiteral("checkpoint"); ev.icon = QStringLiteral("⎘");
    } else {
        ev.category = QStringLiteral("other");       ev.label = t;                            ev.icon = QStringLiteral("·");
    }
}

QString TranscriptService::fmtNumQ(double n)
{
    if (!(n == n)) return QStringLiteral("?");
    if (n >= 1000) return QString::number(n / 1000.0, 'f', 1) + 'k';
    if (n == std::floor(n)) return QString::number(qint64(n));
    return QString::number(n);
}

QString TranscriptService::fmtMsQ(double ms)
{
    if (!(ms == ms)) return QStringLiteral("?");
    if (ms < 1000) return QString::number(qRound(ms)) + QStringLiteral("ms");
    return QString::number(ms / 1000.0, 'f', 1) + 's';
}

QString TranscriptService::summarize(const types::TranscriptEvent& ev)
{
    const QString& t = ev.type;
    const QJsonObject& p = ev.payload;
    const auto pStr = [&p](const char* k) { return js(p, k); };
    const auto pDbl = [&p](const char* k, bool* ok = nullptr) -> double {
        const QJsonValue v = p.value(QLatin1String(k));
        if (v.isDouble()) { if (ok) *ok = true; return v.toDouble(); }
        if (ok) *ok = false;
        return qQNaN();
    };

    if (t == QLatin1String("turn_started")) {
        // truncate(input, 90) — collapse whitespace, cap length
        QString s = pStr("input");
        if (s.isEmpty()) return {};
        static const QRegularExpression ws(QStringLiteral("\\s+"));
        s.replace(ws, QStringLiteral(" "));
        s = s.trimmed();
        return s.size() > 90 ? s.left(90) + QStringLiteral("…") : s;
    }
    if (t == QLatin1String("turn_complete")) {
        bool tokOk = false, durOk = false, toolsOk = false;
        const double tok = pDbl("tokenCount", &tokOk);
        const double dur = pDbl("duration", &durOk);
        const double tools = pDbl("toolCallCount", &toolsOk);
        QString resultType = pStr("resultType");
        if (resultType.isEmpty()) resultType = QStringLiteral("undefined");
        // counts render as bare numbers (JS template literals), not "x.xk"
        return QStringLiteral("result=%1 · tokens=%2 · tools=%3 · %4")
            .arg(resultType,
                 tokOk ? fmtNumQ(tok) : QStringLiteral("?"),
                 toolsOk ? QString::number(qint64(tools)) : QStringLiteral("?"),
                 durOk ? fmtMsQ(dur) : QStringLiteral("?"));
    }
    if (t == QLatin1String("model_request")) {
        bool toolsOk = false, iterOk = false;
        const double tools = pDbl("toolCount", &toolsOk);
        const double iter = pDbl("iteration", &iterOk);
        QString model = pStr("model");
        if (model.isEmpty()) model = QStringLiteral("?");
        return QStringLiteral("%1 · %2 tools · iter %3")
            .arg(model,
                 toolsOk ? QString::number(qint64(tools)) : QStringLiteral("?"),
                 iterOk ? QString::number(qint64(iter)) : QStringLiteral("0"));
    }
    if (t == QLatin1String("model_complete")) {
        const QJsonObject u = p.value(QLatin1String("usage")).toObject();
        const auto uv = [&u](const char* k) -> double {
            const QJsonValue v = u.value(QLatin1String(k));
            return v.isDouble() ? v.toDouble() : qQNaN();
        };
        bool toolsOk = false;
        const double tools = pDbl("toolCallCount", &toolsOk);
        return QStringLiteral("%1 · in=%2 out=%3 cache=%4 · %5 tools")
            .arg(pStr("stopReason"),
                 fmtNumQ(uv("inputTokens")),
                 fmtNumQ(uv("outputTokens")),
                 fmtNumQ(uv("cacheReadTokens")),
                 toolsOk ? QString::number(qint64(tools)) : QStringLiteral("?"));
    }
    if (t == QLatin1String("model_network_status")) {
        const QString inner = pStr("type") == QLatin1String("model_request_started")
            ? QStringLiteral("started") : QStringLiteral("completed");
        QString base = pStr("baseURL");
        static const QRegularExpression ws(QStringLiteral("\\s+"));
        base.replace(ws, QStringLiteral(" "));
        base = base.trimmed();
        if (base.size() > 50) base = base.left(50) + QStringLiteral("…");
        bool attemptOk = false;
        const double attempt = pDbl("attempt", &attemptOk);
        return QStringLiteral("%1 · %2 · attempt %3 · %4")
            .arg(inner, pStr("transport"),
                 attemptOk ? QString::number(qint64(attempt)) : QStringLiteral("?"), base);
    }
    if (t == QLatin1String("model_streaming")) {
        const QString k = pStr("kind");
        const QString delta = pStr("delta");
        if (k == QLatin1String("text_delta"))
            return QStringLiteral("text +") + QString::number(delta.size());
        if (k == QLatin1String("reasoning_delta"))
            return QStringLiteral("think +") + QString::number(delta.size());
        if (k == QLatin1String("tool_input_delta"))
            return QStringLiteral("tool-input +") + QString::number(delta.size());
        return k.isEmpty() ? QStringLiteral("stream") : k;
    }
    if (t == QLatin1String("streaming_tool_ledger_updated")) {
        QStringList flags;
        const QJsonValue ro = p.value(QLatin1String("readOnly"));
        if (ro.toBool()) flags << QStringLiteral("RO");
        const QJsonValue de = p.value(QLatin1String("destructive"));
        if (de.toBool()) flags << QStringLiteral("DESTRUCT");
        QString s = QStringLiteral("%1 · %2").arg(pStr("toolName"), pStr("status"));
        if (!flags.isEmpty())
            s += QStringLiteral(" · ") + flags.join('/');
        return s;
    }
    if (t == QLatin1String("tool_call_scheduled")) {
        const QJsonValue par = p.value(QLatin1String("canRunParallel"));
        return QStringLiteral("%1 · parallel=%2")
            .arg(pStr("toolName"), par.toBool() ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    if (t == QLatin1String("tool_batch_complete")) {
        bool okOk = false, errOk = false;
        const double okN = pDbl("successCount", &okOk);
        const double errN = pDbl("errorCount", &errOk);
        return QStringLiteral("ok=%1 err=%2")
            .arg(okOk ? QString::number(qint64(okN)) : QStringLiteral("?"),
                 errOk ? QString::number(qint64(errN)) : QStringLiteral("?"));
    }
    if (t == QLatin1String("checkpoint_created")) {
        bool filesOk = false;
        const double files = pDbl("fileCount", &filesOk);
        return QStringLiteral("scope=%1 · files=%2")
            .arg(pStr("scope"),
                 filesOk ? QString::number(qint64(files)) : QStringLiteral("?"));
    }
    if (t == QLatin1String("stream_recovery_anchor_created")) {
        return QStringLiteral("%1 · %2").arg(pStr("toolName"), pStr("kind"));
    }
    return {};
}

types::TranscriptAggregate TranscriptService::aggregate(const QVector<types::TranscriptEvent>& events)
{
    types::TranscriptAggregate out;
    // NOTE: unlike JS object literals (first-seen order), QMap sorts by key.
    // The UI re-sorts tools by count and never renders byType order, so this
    // only affects tie ordering. aggregate() intentionally counts ALL events
    // (pre-limit), which is more accurate than the original's sliced input.
    QMap<QString, int> byType;
    QMap<QString, int> tools;
    for (const types::TranscriptEvent& ev : events) {
        byType[ev.type] += 1;
        if (ev.type == QLatin1String("streaming_tool_ledger_updated")) {
            const QString name = js(ev.payload, "toolName");
            if (!name.isEmpty())
                tools[name] += 1;
        }
        if (ev.type == QLatin1String("model_complete")) {
            const QJsonObject u = ev.payload.value(QLatin1String("usage")).toObject();
            const auto uv = [&u](const char* k) -> qint64 {
                const QJsonValue v = u.value(QLatin1String(k));
                return v.isDouble() ? qint64(v.toDouble()) : 0;
            };
            out.tokensInput += uv("inputTokens");
            out.tokensOutput += uv("outputTokens");
            out.tokensCache += uv("cacheReadTokens");
        }
    }
    for (auto it = byType.constBegin(); it != byType.constEnd(); ++it)
        out.byType.push_back({it.key(), it.value()});
    for (auto it = tools.constBegin(); it != tools.constEnd(); ++it)
        out.tools.push_back({it.key(), it.value()});
    return out;
}
