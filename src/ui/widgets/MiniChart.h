#pragma once
// MiniChart.h — QPainter-drawn replacement for the 4 Chart.js charts:
//   · bar chart (calls per hour)
//   · multi-series line chart with fills (token mix)
//   · single line with per-point colors (tok/s)
// Includes grid, ticks, legend, rotated y-axis title and hover tooltip
// (matching chart.js defaults used by the web version).

#include <QColor>
#include <QStringList>
#include <QVector>
#include <QWidget>

class MiniChart : public QWidget {
    Q_OBJECT
public:
    struct Series {
        QString name;
        QColor color;
        QVector<double> values;
        double fillAlpha = 0;           // 0 → no fill
        bool showPoints = false;
        QVector<QColor> pointColors;    // optional per-point colors
        double lineWidth = 1.5;
    };

    enum YFormat { YInt, YNum, YTps };

    explicit MiniChart(QWidget* parent = nullptr);

    void setBar(const QStringList& labels, const QVector<double>& values,
                const QColor& color, const QString& yTitle, YFormat fmt = YInt);
    void setLines(const QStringList& labels, const QVector<Series>& series,
                  const QString& yTitle, YFormat fmt = YNum);
    void setEmpty(const QString& text);   // clear + empty-state note
    void clear();

    QSize minimumSizeHint() const override { return QSize(240, 200); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    struct TickInfo {
        double step = 1;
        double max = 1;
    };
    TickInfo niceTicks(double maxVal) const;
    QRect plotRect() const;
    QString fmtY(double v) const;
    int hoverIndexAt(const QPoint& pos) const;
    void paintLegend(QPainter& p, int& topPad) const;

    bool m_isBar = false;
    QStringList m_labels;
    QVector<Series> m_series;
    QColor m_barColor;
    QString m_yTitle;
    YFormat m_fmt = YInt;
    QString m_emptyText;
    int m_hoverIndex = -1;
};
