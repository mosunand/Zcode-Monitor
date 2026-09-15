#pragma once
// ContextTab.h — port of renderContext in sessions.js: full conversation
// replay with a per-turn rail (left) and message stream (right), scroll
// linkage both ways, collapsible reasoning/tool parts and lazy tool output.

#include <QSet>
#include <QWidget>

#include <functional>

#include "core/Types.h"

class QListWidget;
class QScrollArea;
class QVBoxLayout;
class QLabel;
class QPushButton;

class ContextTab : public QWidget {
    Q_OBJECT
public:
    explicit ContextTab(QWidget* parent = nullptr);
    void load(const QString& sessionId);

signals:
    void openSessionRequested(const QString& sessionId, const QString& tab);

private:
    void buildUi();
    void render(const QVector<types::Message>& messages,
                const QVector<types::TurnRow>& turns);
    QWidget* buildMessageWidget(const types::Message& m, int msgIndex);
    QWidget* buildPartWidget(const types::Part& p);
    void scrollToTurn(int turnIdx);
    void updateRailHighlight();
    void setAllExpanded(bool expanded);

    QString m_sessionId;
    QListWidget* m_rail = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_convHost = nullptr;
    QVBoxLayout* m_convLay = nullptr;
    QVector<QWidget*> m_msgWidgets;       // indexed by message
    QVector<int> m_msgTurnIdx;            // message → turn index
    QVector<std::function<void(bool)>> m_expanders;
    QSet<QString> m_restoreExpanded;      // "msgIdx:partIdx" round-trip on theme flips
    int m_requestSeq = 0;                 // async load guard: drop superseded results
};
