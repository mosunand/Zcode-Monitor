#pragma once
// HowPage.h — port of public/views/how.js: "运行原理" explainer page using
// the user's real data as examples.

#include <QWidget>

#include "core/Types.h"

class QLabel;

class HowPage : public QWidget {
    Q_OBJECT
public:
    explicit HowPage(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* e) override;

signals:
    void openSessionRequested(const QString& sessionId, const QString& tab);
    void navigateRequested(const QString& page);

private:
    void buildUi();
    void loadConcepts();   // async: overview + forest + reasoning in one trip
    void applyConcepts(const types::OverviewData& ov,
                       const QVector<types::AgentNode>& forest); // GUI-thread half
    void loadReasonExample();
    void applyReason(const QString& sample); // store + re-apply
    QString reasonIntroHtml() const; // palette-tinted rich text (rebuilt on theme flips)

    QWidget* m_conceptsHost = nullptr;
    QLabel* m_reasonExample = nullptr;
    QLabel* m_reasonIntroLabel = nullptr;
    QString m_reasonText; // last fetched reasoning sample
    bool m_loaded = false;
};
