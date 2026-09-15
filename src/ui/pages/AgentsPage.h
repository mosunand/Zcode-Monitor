#pragma once
// AgentsPage.h — port of public/views/agents.js: subagent relationship tree.

#include <QWidget>

#include "core/Types.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

class AgentsPage : public QWidget {
    Q_OBJECT
public:
    explicit AgentsPage(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* e) override;

signals:
    void openSessionRequested(const QString& sessionId, const QString& tab);

private:
    void load();
    void applyForest(const QVector<types::AgentNode>& forest); // GUI-thread half

    QLabel* m_summary = nullptr;
    QTreeWidget* m_tree = nullptr;
    bool m_loaded = false;
    int m_loadSeq = 0; // async load guard: drop superseded results
};
