#pragma once
// LogTailService.h — port of server/log-tail.js.
// Reads the daily JSONL logs under ~/.zcode/cli/log/ (UTC day files):
//  · tailLog — efficient last-N-lines read (tail up to 1MB)
//  · eventsForTrace — scan today + yesterday for a traceId
//  · buildSpanForest — span tree for the trace waterfall

#include <QVector>

#include "Types.h"

class LogTailService {
public:
    static QString todayLogFile();                 // logDir/zcode-<UTC date>.jsonl
    static QString logFileFor(const QDate& utcDate);

    static QVector<types::LogEvent> tailLog(int lines = 200);
    static QVector<types::LogEvent> eventsForTrace(const QString& traceId);
    static QVector<types::SpanNode> buildSpanForest(const QVector<types::LogEvent>& events);

private:
    static types::LogEvent parseLine(const QString& line);
};
