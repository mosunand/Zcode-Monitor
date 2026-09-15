#pragma once
// SessionsPage.h — port of public/views/sessions.js: left session list +
// right detail with the 7 tabs (Timeline/Context/Turns/Agents/Tasks/Usage/State).

#include <QWidget>

#include "core/Types.h"

class QComboBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedLayout;
class QTableWidget;
class QTabWidget;
class QLabel;
class TimelineTab;
class ContextTab;

class SessionsPage : public QWidget {
    Q_OBJECT
public:
    explicit SessionsPage(QWidget* parent = nullptr);

    // deep link #/sessions/{id}/{tab}
    void openSession(const QString& sessionId, const QString& tab);

signals:
    void openSessionRequested(const QString& sessionId, const QString& tab);

protected:
    void showEvent(QShowEvent* e) override;

private slots:
    void loadList();
    void renderList();
    void onListItemClicked(int row);
    void onTabChanged(int index);
    void loadDetailHead();
    void loadTab();

private:
    void buildUi();
    QWidget* buildTurnsTab();
    QWidget* buildAgentsTab();
    QWidget* buildTasksTab();
    QWidget* buildUsageTab();
    QWidget* buildStateTab();
    void renderTurns(const QVector<types::TurnRow>& turns);
    void renderAgents(const QVector<types::ChildAgent>& children);
    void renderTasks(const QVector<types::TodoRow>& todos);
    void renderUsage(const QVector<types::TurnRow>& turns);
    void renderState(const types::SessionDetail& detail);

    // left pane
    QLineEdit* m_search = nullptr;
    QComboBox* m_taskType = nullptr;
    QComboBox* m_sort = nullptr;
    QListWidget* m_list = nullptr;

    // right pane
    QLabel* m_title = nullptr;
    QLabel* m_titleSub = nullptr;
    QStackedLayout* m_detailStack = nullptr; // 0 = tabs, 1 = "pick a session" placeholder
    QTabWidget* m_tabs = nullptr;
    QWidget* m_turnsTab = nullptr;
    QWidget* m_agentsTab = nullptr;
    QWidget* m_tasksTab = nullptr;
    QWidget* m_usageTab = nullptr;
    QWidget* m_stateTab = nullptr;
    TimelineTab* m_timelineTab = nullptr;
    ContextTab* m_contextTab = nullptr;

    QVector<types::SessionRow> m_listData;
    QString m_currentId;
    QString m_currentTab;
    bool m_listLoaded = false;
    int m_listSeq = 0;  // async list-load guard: drop superseded results
    int m_headSeq = 0;  // async detail-head guard
    int m_tabSeq = 0;   // async tab-content guard
};
