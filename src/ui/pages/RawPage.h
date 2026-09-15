#pragma once
// RawPage.h — port of public/views/raw.js: raw table browser (read-only,
// allowlisted tables, free-text where like the original debugging tool).

#include <QWidget>

#include "core/Types.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QCheckBox;

class RawPage : public QWidget {
    Q_OBJECT
public:
    explicit RawPage(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* e) override;

private slots:
    void load();

private:
    void buildUi();
    void applyResult(const types::RawResult& r); // GUI-thread half of load()

    QComboBox* m_tableCombo = nullptr;
    QComboBox* m_orderCombo = nullptr;
    QCheckBox* m_descCheck = nullptr;
    QLineEdit* m_whereInput = nullptr;
    QPushButton* m_goBtn = nullptr;
    QLabel* m_note = nullptr;
    QLabel* m_count = nullptr;
    QTableWidget* m_table = nullptr;
    bool m_loaded = false;
    int m_loadSeq = 0; // async query guard: drop superseded results
};
