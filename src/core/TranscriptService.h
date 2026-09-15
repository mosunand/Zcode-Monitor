#pragma once
// TranscriptService.h — port of server/transcript.js.
// Parses a subagent's transcript.jsonl + metadata.json under
// ~/.zcode/cli/agents/<parent>/agent_<uuid>/.

#include <QString>
#include <QVector>

#include "Types.h"

class TranscriptService {
public:
    struct AgentLocation {
        bool found = false;
        QString parentSessionId;
        QString agentId; // "agent_<uuid>"
        QString dir;
        QJsonObject meta;
    };

    // trailing agent_<uuid> segment of sess_subagent_agent_<uuid>
    static QString agentUuidFromChild(const QString& childSessionId);

    static AgentLocation locateAgent(const QString& childSessionId);

    // readTranscript: events (parsed, filtered, limited) + meta.
    // types filter: empty list = no filter. limit <= 0 = unlimited.
    static types::TranscriptData readTranscript(const QString& childSessionId,
                                                int limit = 0,
                                                const QStringList& typeFilter = {});

    // event display mapping (category / label / icon / summary)
    static void categorize(types::TranscriptEvent& ev);
    static QString summarize(const types::TranscriptEvent& ev);

    static types::TranscriptAggregate aggregate(const QVector<types::TranscriptEvent>& events);

    // local formatters used by summarize (from transcript.js)
    static QString fmtNumQ(double n);  // null → "?", >=1000 → "x.xk"
    static QString fmtMsQ(double ms);   // null → "?"
};
