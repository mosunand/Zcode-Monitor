#pragma once
// KpiCard.h — the .kpi card from styles.css: uppercase label, big value,
// delta line, optional 3px progress bar (cache-hit rate).

#include <QFrame>
#include <QGridLayout>
#include <QString>

class QLabel;

class KpiCard : public QFrame {
    Q_OBJECT
public:
    explicit KpiCard(const QString& labelText, QWidget* parent = nullptr);

    void setValue(const QString& text, const QColor& color = QColor());
    void setDelta(const QString& text);
    // optional rich-text detail block (e.g. per-model usage lines)
    void setDetails(const QString& html);
    // 大数值模式：独占两行的大卡用，数值 30pt（默认 19pt）
    void setLargeValue(bool on);
    // replace the caption text (e.g. "模型调用 (24h)")
    void setCaption(const QString& text);
    // pct in 0..100; color empty → hidden (pct < 0 also hides)
    void setBar(int pct, const QColor& color);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QLabel* m_label;
    QLabel* m_value;
    QLabel* m_delta;
    QLabel* m_details = nullptr;
    int m_barPct = -1;
    QColor m_barColor;
    bool m_largeValue = false;
};
