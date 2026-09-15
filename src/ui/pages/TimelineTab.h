#pragma once
// TimelineTab.h — port of public/views/timeline.js: the wire-style event
// timeline from transcript.jsonl, with category filters, streaming-delta
// coalescing and expandable payloads.

#include <QWidget>

#include "core/Types.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class QVBoxLayout;

class TimelineTab : public QWidget {
    Q_OBJECT
public:
    explicit TimelineTab(QWidget* parent = nullptr);
    void load(const QString& sessionId);

signals:
    void openSessionRequested(const QString& sessionId, const QString& tab);

private:
    void buildUi();
    void renderNotFound(const QString& sessionId);
    void renderHeader();
    void renderEvents();
    void fillTree();
    void setFilter(const QString& filterId);
    void loadData(); // reload transcript data + rebuild UI (keeps m_activeFilter)
    void applyData(const types::TranscriptData& d, const QString& sessionId);

    QString m_sessionId;
    types::TranscriptData m_data;
    QString m_activeFilter = QStringLiteral("all");
    int m_requestSeq = 0; // bumps per load; stale async results are dropped

    QVBoxLayout* m_body = nullptr;
    QWidget* m_headerCard = nullptr;
    QWidget* m_filterBar = nullptr;
    QLabel* m_countLabel = nullptr;
    QTreeWidget* m_tree = nullptr;
};
