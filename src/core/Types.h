#pragma once
// Types.h — shared data structures for the whole app.
// Mirrors the JSON shapes of the original zcode-monitor (Node/Express) backend.
// Time values are epoch milliseconds (qint64) throughout; the original code
// converted to ISO strings for the wire, we keep ms internally instead.

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

namespace types {

// ───────────────────────── Overview ─────────────────────────

struct Kpis {
    // model_usage aggregates
    qint64 calls = 0, completed = 0, errors = 0, cancelled = 0;
    bool avgDurationOk = false;
    double avgDurationMs = 0;
    // tool_usage aggregates
    qint64 toolCalls = 0, toolErrors = 0;
    bool toolAvgOk = false;
    double toolAvgMs = 0;
    // tokens
    qint64 inTok = 0, outTok = 0, reasonTok = 0, cacheRead = 0, cacheWrite = 0;
    bool reasonRatioOk = false;
    double reasonRatio = 0; // reasoning / (reasoning + output)
    qint64 activeSessions = 0;
    qint64 sinceMs = 0, windowMs = 0;
};

struct SeriesPoint {
    qint64 bucketMs = 0;
    qint64 calls = 0, input = 0, output = 0, reasoning = 0, errors = 0;
};

struct ModelBreakdown {
    QString providerId, modelId, variant, querySource;
    qint64 calls = 0, inTok = 0, outTok = 0, reasonTok = 0;
    bool avgOk = false;
    double avgMs = 0;
};

struct ToolBreakdown {
    QString toolName;
    qint64 calls = 0, errors = 0;
    bool avgOk = false;
    double avgMs = 0;
    qint64 maxMs = 0;
    qint64 outBytes = 0;
};

struct SpeedStats {
    bool weightedOk = false;
    double weightedTps = 0;
    qint64 totalTokens = 0;
    double totalSeconds = 0;
    qint64 requestCount = 0, mainCount = 0, subagentCount = 0;
};

struct SpeedRow {
    qint64 timeMs = 0;
    QString model;
    qint64 output = 0, reasoning = 0, durationMs = 0;
    bool tpsOk = false;
    double tps = 0;
    QString querySource;
};

struct OverviewData {
    QString window; // "today" | "24h" | "7d"
    qint64 sinceMs = 0;
    Kpis kpis;
    QVector<SeriesPoint> series;
    QVector<ModelBreakdown> byModel;
    QVector<ToolBreakdown> byTool;
    SpeedStats speed;
    QVector<SpeedRow> recentSpeed;
};

// ───────────────────────── Live feed ─────────────────────────

struct ModelRow {
    qint64 id = 0;
    qint64 tailRid = 0; // rowid at fetch time — live-feed cursor
    QString sessionId, turnId, traceId, status;
    qint64 startedMs = 0, durationMs = 0;
    QString querySource, modelId, variant, mode, agent;
    qint64 inputTokens = 0, outputTokens = 0, reasoningTokens = 0, toolCallCount = 0;
    QString errorType;
};

struct ToolRow {
    qint64 id = 0;
    qint64 tailRid = 0; // rowid at fetch time — live-feed cursor
    QString sessionId, turnId, traceId, toolCallId, toolName, status;
    qint64 startedMs = 0, durationMs = 0;
    bool hasExit = false;
    qint64 exitCode = 0;
    QString errorType;
};

// ───────────────────────── Health / runtime ─────────────────────────

struct WalStatus {
    bool valid = false;
    qint64 mainBytes = 0, walBytes = 0, shmBytes = 0;
};

struct CheckpointResult {
    bool ok = false;
    QString error;
    WalStatus before, after;
    int busy = -1;         // sqlite wal_checkpoint busy flag (0/1), -1 unknown
    int checkpointed = -1; // folded frame count, -1 unknown
};

struct CheckpointRecord {
    bool valid = false;   // any checkpoint happened yet
    qint64 atMs = 0;
    bool ok = false;
    qint64 walBefore = 0, walAfter = 0;
    bool foldedOk = false;
    qint64 folded = 0;
    QString error;
};

struct Health {
    bool ok = false;
    QString error;
    QString db, logDir;
    bool zcodeRunning = true;
    WalStatus wal;
    CheckpointRecord lastCheckpoint;
};

// ───────────────────────── Sessions ─────────────────────────

struct SessionRow {
    QString id, title, taskType, directory, parentId;
    qint64 timeCreated = 0, timeUpdated = 0;
    qint64 modelCalls = 0, toolCalls = 0;
    bool totalTokensOk = false;
    qint64 totalTokens = 0;
};

struct TurnRow {
    QString turnId, status, traceId, userMessageId;
    qint64 startedMs = 0, firstTokenMs = 0, completedMs = 0;
    bool durationOk = false;   qint64 durationMs = 0;
    bool ttftOk = false;       qint64 ttftMs = 0;
    qint64 modelRequestCount = 0, modelRetryCount = 0;
    qint64 toolCallCount = 0, toolErrorCount = 0;
    qint64 inputTokens = 0, outputTokens = 0, reasoningTokens = 0;
    qint64 cacheReadTokens = 0, cacheWriteTokens = 0, computedTotalTokens = 0;
    bool contextExceeded = false;
    QString errorType, errorCode;
};

struct Part {
    QString id;
    qint64 sequence = 0;
    qint64 timeCreatedMs = 0;
    QJsonObject data;
};

struct Message {
    QString id;
    qint64 sequence = 0;
    qint64 timeCreatedMs = 0;
    QString role, model, provider, variant, mode, agent;
    QJsonObject tokens; // may be empty
    QString turnId;     // from data.anchor.turnId
    QJsonObject env;    // user message contextSnapshot.envInfo
    QVector<Part> parts;
};

struct ChildAgent {
    QString id, title, taskType;
    bool totalTokensOk = false;
    qint64 totalTokens = 0;
    qint64 timeCreatedMs = 0, timeUpdatedMs = 0;
    QString profile;                 // metadata.profileId
    QJsonObject profileSnapshot;    // metadata.profileSnapshot
    QString prompt;                  // metadata.prompt
    QString parentToolUseId;         // metadata.parentToolUseId
};

struct ActivityRow {
    QString kind; // "model" | "tool"
    // model fields
    QString turnId, status, querySource, modelId, providerId, variant, mode, agent;
    qint64 inputTokens = 0, outputTokens = 0, reasoningTokens = 0, toolCallCount = 0;
    QString errorType, errorMessage;
    // tool fields
    QString toolCallId, toolName;
    bool hasExit = false;
    qint64 exitCode = 0;
    // shared
    qint64 id = 0;
    QString sessionId, traceId;
    qint64 startedMs = 0, completedMs = 0, durationMs = 0;
};

struct ReasoningItem {
    QString id, messageId;
    qint64 timeMs = 0;
    QString text;
    bool timeStartOk = false; qint64 timeStartMs = 0;
    bool timeEndOk = false;   qint64 timeEndMs = 0;
};

struct ToolOutput {
    bool found = false;    // session exec dir existed
    QString stdout_;       // capped at 200KB
    QString stderr_;       // capped at 64KB
    bool truncated = false;
};

// full session row (SELECT * FROM session) as key/value map, plus the
// convenience fields the State tab renders.
struct SessionDetail {
    bool found = false;
    QString id, title, taskType, parentId, workspaceId, projectId;
    QString directory, permission, traceId;
    qint64 timeCreated = 0, timeUpdated = 0;
    QVariantMap allColumns;
};

// ───────────────────────── Agents forest ─────────────────────────

struct AgentNode {
    QString id, title, taskType, parentId, directory;
    qint64 timeCreated = 0, timeUpdated = 0;
    bool tokensOk = false;
    qint64 tokens = 0;
    qint64 modelCalls = 0, toolCalls = 0;
    QVector<AgentNode> children;
};

// ───────────────────────── Errors & trace ─────────────────────────

struct KVCount {
    QString k;
    qint64 n = 0;
};

struct ErrorSummary {
    QVector<KVCount> byModelErrorType, byToolName, byToolErrorType;
};

struct FailedModelRow {
    qint64 id = 0;
    QString sessionId, turnId, traceId, status;
    qint64 startedMs = 0;
    QString modelId, providerId, querySource;
    qint64 inputTokens = 0, outputTokens = 0;
    bool durationOk = false; qint64 durationMs = 0;
    QString errorType, errorCode, errorMessage;
};

struct FailedToolRow {
    qint64 id = 0;
    QString sessionId, turnId, traceId, toolCallId, toolName, status;
    qint64 startedMs = 0;
    bool durationOk = false; qint64 durationMs = 0;
    bool hasExit = false; qint64 exitCode = 0;
    qint64 stderrBytes = 0;
    QString errorType, errorCode, errorMessage;
};

struct FailedItems {
    QVector<FailedModelRow> model;
    QVector<FailedToolRow> tool;
};

struct SlowToolRow {
    qint64 id = 0;
    QString sessionId, turnId, traceId, toolCallId, toolName, status;
    qint64 startedMs = 0;
    bool durationOk = false; qint64 durationMs = 0;
    bool hasExit = false; qint64 exitCode = 0;
    QString err; // first 160 chars of error_message
};

// ───────────────────────── Transcript (timeline tab) ─────────────────────────

struct TranscriptEvent {
    qint64 sequenceNumber = 0;
    bool seqOk = false;
    QString type;
    qint64 timestampMs = 0;
    bool tsOk = false;
    QString timestampRaw; // ISO as found in the file
    QString turnId, traceId, sessionId;
    QJsonObject payload;
    // derived (categorize/summarize)
    QString category, label, icon, summary;
};

struct TranscriptMeta {
    bool valid = false;
    QString agentId, profileId, childSessionId, parentSessionId, parentToolUseId;
    QString cwd, description, status;
    qint64 createdAtMs = 0;   bool createdAtOk = false;
    qint64 completedAtMs = 0; bool completedAtOk = false;
    bool totalDurationOk = false; qint64 totalDurationMs = 0;
    bool totalTokensOk = false;  qint64 totalTokens = 0;
    bool totalToolUseOk = false; qint64 totalToolUseCount = 0;
    QJsonObject usage;
    QJsonObject raw; // full metadata.json
};

struct TranscriptAggregate {
    QVector<KVCount> byType; // in first-seen order
    QVector<KVCount> tools;  // sorted by count desc happens at display
    qint64 tokensInput = 0, tokensOutput = 0, tokensCache = 0;
};

struct TranscriptData {
    bool found = false;
    QString message; // when not found
    TranscriptMeta meta;
    qint64 count = 0; // events after type filter, before limit
    QVector<TranscriptEvent> events;
    TranscriptAggregate aggregate;
};

// ───────────────────────── Log tail / trace waterfall ─────────────────────────

struct LogEvent {
    QJsonObject raw;
    qint64 timestampMs = 0;
    bool tsOk = false;
    QString timestampRaw;
    QString level, event, module, message, traceId, spanId, parentSpanId;
    QString sessionId, turnId, status;
    bool durationOk = false;
    qint64 durationMs = 0;
};

struct SpanNode {
    LogEvent event;
    QVector<SpanNode> children;
};

// ───────────────────────── Raw table viewer ─────────────────────────

struct RawResult {
    bool ok = false;
    QString error;
    QString table;
    qint64 count = 0;
    int limit = 0, offset = 0;
    QStringList columns;          // column order of the first row
    QVector<QVariantMap> rows;    // column name → value
};

// ───────────────────────── Todo (Tasks tab) ─────────────────────────

struct TodoRow {
    QString status, priority, content;
};

} // namespace types
