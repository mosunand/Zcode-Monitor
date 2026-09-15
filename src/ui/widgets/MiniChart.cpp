// MiniChart.cpp — see MiniChart.h.

#include "MiniChart.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

#include "ui/Theme.h"
#include "ui/UiUtil.h"
#include "util/Format.h"

namespace {

// Fritsch–Carlson monotone cubic interpolation: a smooth curve through pts
// with no overshoot — the rounded look chart.js gives its line charts,
// instead of hard straight segments.
QPainterPath smoothPath(const QVector<QPoint>& pts)
{
    QPainterPath path;
    const int n = pts.size();
    if (n == 0)
        return path;
    if (n == 1) {
        path.moveTo(pts[0]);
        return path;
    }
    if (n == 2) {
        path.moveTo(pts[0]);
        path.lineTo(pts[1]);
        return path;
    }

    QVector<double> sec(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        const double dx = pts[i + 1].x() - pts[i].x();
        sec[i] = double(pts[i + 1].y() - pts[i].y()) / qMax(1.0, dx);
    }

    QVector<double> m(n); // tangent slopes
    for (int i = 0; i < n; ++i) {
        if (i == 0) {
            m[i] = sec[0];
        } else if (i == n - 1) {
            m[i] = sec[n - 2];
        } else {
            const double s1 = sec[i - 1], s2 = sec[i];
            if (s1 * s2 <= 0.0) {
                m[i] = 0.0; // local extremum: flat tangent, no overshoot
            } else {
                const double d1 = pts[i].x() - pts[i - 1].x();
                const double d2 = pts[i + 1].x() - pts[i].x();
                const double w1 = 2 * d2 + d1;
                const double w2 = d2 + 2 * d1;
                m[i] = (w1 + w2) / (w1 / s1 + w2 / s2);
            }
        }
    }

    path.moveTo(pts[0]);
    for (int i = 0; i < n - 1; ++i) {
        const double h = qMax(1.0, double(pts[i + 1].x() - pts[i].x()));
        const QPointF c1(pts[i].x() + h / 3.0, pts[i].y() + m[i] * h / 3.0);
        const QPointF c2(pts[i + 1].x() - h / 3.0, pts[i + 1].y() - m[i + 1] * h / 3.0);
        path.cubicTo(c1, c2, QPointF(pts[i + 1]));
    }
    return path;
}

} // namespace

MiniChart::MiniChart(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(200);
}

void MiniChart::setBar(const QStringList& labels, const QVector<double>& values,
                       const QColor& color, const QString& yTitle, YFormat fmt)
{
    m_isBar = true;
    m_labels = labels;
    m_series.clear();
    Series s;
    s.color = color;
    s.values = values;
    m_series.push_back(s);
    m_barColor = color;
    m_yTitle = yTitle;
    m_fmt = fmt;
    m_emptyText.clear();
    m_hoverIndex = -1;
    update();
}

void MiniChart::setLines(const QStringList& labels, const QVector<Series>& series,
                         const QString& yTitle, YFormat fmt)
{
    m_isBar = false;
    m_labels = labels;
    m_series = series;
    m_barColor = QColor();
    m_yTitle = yTitle;
    m_fmt = fmt;
    m_emptyText.clear();
    m_hoverIndex = -1;
    update();
}

void MiniChart::setEmpty(const QString& text)
{
    clear();
    m_emptyText = text;
    update();
}

void MiniChart::clear()
{
    m_labels.clear();
    m_series.clear();
    m_barColor = QColor();
    m_yTitle.clear();
    m_emptyText.clear();
    m_hoverIndex = -1;
    update();
}

MiniChart::TickInfo MiniChart::niceTicks(double maxVal) const
{
    TickInfo t;
    if (maxVal <= 0) {
        t.step = 1;
        t.max = 5;
        return t;
    }
    const double mag = std::pow(10.0, std::floor(std::log10(maxVal)));
    const double norm = maxVal / mag; // 1..10
    double nice;
    if (norm <= 1.0)       nice = 1.0;
    else if (norm <= 2.0)  nice = 2.0;
    else if (norm <= 2.5)  nice = 2.5;
    else if (norm <= 5.0)  nice = 5.0;
    else                   nice = 10.0;
    const double step = nice * mag / 5.0;
    t.step = step;
    t.max = step * 5.0;
    return t;
}

QRect MiniChart::plotRect() const
{
    int topPad = 10;
    if (!m_isBar && m_series.size() > 1)
        topPad = 30; // legend row
    return rect().adjusted(52, topPad, -10, -46);
}

QString MiniChart::fmtY(double v) const
{
    switch (m_fmt) {
    case YInt: return QString::number(qRound64(v));
    case YTps: return QString::number(v, 'f', 1);
    case YNum:
    default:   return Format::fmtNum(v);
    }
}

void MiniChart::paintLegend(QPainter& p, int& topPad) const
{
    if (m_isBar || m_series.size() < 2)
        return;
    const Palette& pal = Theme::instance().pal();
    QFont f = p.font();
    f.setPointSizeF(7.5);
    p.setFont(f);
    QFontMetrics fm(f);
    int x = plotRect().left();
    const int y = 8;
    for (const Series& s : m_series) {
        p.setPen(Qt::NoPen);
        p.setBrush(s.color);
        p.drawRect(QRect(x, y + 3, 10, 4));
        x += 14;
        p.setPen(pal.chartLegend);
        p.drawText(QPoint(x, y + fm.height() - 3), s.name);
        x += fm.horizontalAdvance(s.name) + 14;
    }
    topPad = y + fm.height() + 6;
}

void MiniChart::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette& pal = Theme::instance().pal();

    if (m_labels.isEmpty() || m_series.isEmpty()) {
        if (!m_emptyText.isEmpty()) {
            p.setPen(pal.fg4);
            QFont f = p.font();
            f.setPointSizeF(9);
            p.setFont(f);
            p.drawText(rect(), Qt::AlignCenter, m_emptyText);
        }
        return;
    }

    const int n = m_labels.size();
    if (n == 0)
        return;

    // ── y scale ──
    double maxVal = 0;
    for (const Series& s : m_series)
        for (double v : s.values)
            maxVal = qMax(maxVal, v);
    const TickInfo ticks = niceTicks(maxVal);

    int topPad = 10;
    paintLegend(p, topPad);
    const QRect plot = rect().adjusted(52, topPad, -10, -46);
    if (plot.width() <= 10 || plot.height() <= 10)
        return;

    QFont base = p.font();
    QFont small = base;
    small.setPointSizeF(8.5);

    // ── grid + y ticks ──
    p.setFont(small);
    for (int i = 0; i <= 5; ++i) {
        const double frac = i / 5.0;
        const int y = int(plot.bottom() - frac * plot.height());
        // baseline stronger than intermediate grid lines
        p.setPen(QPen(i == 0 ? pal.chartTick : pal.chartGrid, 1));
        p.drawLine(plot.left(), y, plot.right(), y);
        // small tick marks on the axis itself
        p.setPen(QPen(pal.chartTick, 1));
        p.drawLine(plot.left() - 3, y, plot.left(), y);
        p.drawText(QRect(plot.left() - 48, y - 8, 44, 16),
                   Qt::AlignRight | Qt::AlignVCenter, fmtY(ticks.step * i));
    }

    // ── x labels (dense: one point per half hour) + vertical grid ──
    const int labelPad = 45; // 大图空间充足 → 标签更密（约每半小时一个点位）
    const int maxLabels = qMax(1, plot.width() / labelPad);
    const int skip = qMax(1, qCeil(double(n) / maxLabels));
    p.setPen(pal.chartTick);
    for (int i = 0; i < n; i += skip) {
        double x;
        if (m_isBar || n == 1)
            x = plot.left() + (i + 0.5) * double(plot.width()) / n;
        else
            x = plot.left() + double(i) * plot.width() / (n - 1);
        const int xi = int(x);
        p.setPen(QPen(pal.chartGridX, 1));
        p.drawLine(xi, plot.top(), xi, plot.bottom());
        p.setPen(pal.chartTick);
        QFontMetrics fm(small);
        // 两行标签：第一行 HH:mm，第二行 月/日（各行独立省略，互不重叠）
        const QStringList parts = m_labels[i].split(QLatin1Char('\n'));
        const int lineW = labelPad + 15;
        const int y1 = plot.bottom() + 3;
        const int y2 = y1 + fm.height();
        p.drawText(QRect(xi - lineW / 2, y1, lineW, fm.height()),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   fm.elidedText(parts.value(0), Qt::ElideRight, lineW));
        if (parts.size() > 1)
            p.drawText(QRect(xi - lineW / 2, y2, lineW, fm.height()),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       fm.elidedText(parts.value(1), Qt::ElideRight, lineW));
    }
    // 半小时细分网格线（相邻主刻度之间画一条更淡的竖线）
    if (skip == 1 && n <= 48 && n >= 2) {
        QColor minor = pal.chartGridX;
        minor.setAlpha(qRound(minor.alpha() * 0.5));
        p.setPen(QPen(minor, 1));
        const double stepX = double(plot.width()) / (m_isBar ? n : (n - 1));
        for (int i = 0; i < n - 1; ++i) {
            double xMid;
            if (m_isBar)
                xMid = plot.left() + (i + 1.0) * stepX; // 柱与柱之间的中线
            else
                xMid = plot.left() + (i + 0.5) * stepX;
            p.drawLine(int(xMid), plot.top(), int(xMid), plot.bottom());
        }
    }

    const double yScale = plot.height() / ticks.max;

    // ── bars ──
    if (m_isBar) {
        const Series& s = m_series.first();
        const double bw = double(plot.width()) / n * 0.8;
        QColor fill = m_barColor;
        fill.setAlpha(qRound(fill.alpha() * 0.55));
        for (int i = 0; i < n; ++i) {
            const double v = s.values.value(i);
            const int h = qRound(v * yScale);
            const double xc = plot.left() + (i + 0.5) * double(plot.width()) / n;
            QRect bar(int(xc - bw / 2), plot.bottom() - h, qMax(1, int(bw)), h);
            if (bar.height() > 0) {
                // rounded top corners (web chart.js bar radius look)
                p.setPen(QPen(m_barColor, 1));
                p.setBrush(fill);
                p.drawRoundedRect(bar, qMin<qreal>(2.0, bw / 4.0),
                                  qMin<qreal>(2.0, h / 4.0));
            }
        }
    } else {
        // ── lines ──
        for (const Series& s : m_series) {
            if (s.values.isEmpty())
                continue;
            QVector<QPoint> pts;
            for (int i = 0; i < n && i < s.values.size(); ++i) {
                const double x = (n == 1)
                    ? plot.center().x()
                    : plot.left() + double(i) * plot.width() / (n - 1);
                const int y = int(plot.bottom() - s.values[i] * yScale);
                pts.push_back(QPoint(qRound(x), y));
            }
            if (pts.isEmpty())
                continue;
            // smooth curve through the points (monotone cubic — the rounded
            // look of chart.js, with no overshoot on spiky data)
            QPainterPath path = smoothPath(pts);

            if (s.fillAlpha > 0) {
                QPainterPath fillPath = path;
                fillPath.lineTo(pts.last().x(), plot.bottom());
                fillPath.lineTo(pts.first().x(), plot.bottom());
                fillPath.closeSubpath();
                QColor fc = s.color;
                fc.setAlpha(qRound(s.fillAlpha * 255));
                p.setPen(Qt::NoPen);
                p.setBrush(fc);
                p.drawPath(fillPath);
            }
            p.setPen(QPen(s.color, s.lineWidth));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);

            if (s.showPoints) {
                for (int i = 0; i < pts.size(); ++i) {
                    const QColor pc = s.pointColors.value(i, s.color);
                    p.setPen(Qt::NoPen);
                    p.setBrush(pc);
                    const int r = (i == m_hoverIndex) ? 5 : 3;
                    p.drawEllipse(pts[i], r, r);
                }
            }
        }
    }

    // ── y axis title (rotated) ──
    if (!m_yTitle.isEmpty()) {
        p.save();
        p.translate(12, plot.center().y());
        p.rotate(-90);
        p.setPen(pal.chartTick);
        QFont tf = p.font();
        tf.setPointSizeF(7.5);
        p.setFont(tf);
        p.drawText(QRect(-200, -12, 400, 16), Qt::AlignCenter, m_yTitle);
        p.restore();
    }

    // ── hover crosshair + tooltip ──
    if (m_hoverIndex >= 0 && m_hoverIndex < n) {
        double xh;
        if (m_isBar || n == 1)
            xh = plot.left() + (m_hoverIndex + 0.5) * double(plot.width()) / n;
        else
            xh = plot.left() + double(m_hoverIndex) * plot.width() / (n - 1);
        p.setPen(QPen(pal.chartTick, 1, Qt::DashLine));
        p.drawLine(int(xh), plot.top(), int(xh), plot.bottom());

        // tooltip content: label + value per series（字号加大，读得清）
        QFont tf = p.font();
        tf.setPointSizeF(9.5);
        QFontMetrics tfm(tf);
        QStringList lines;
        lines << m_labels[m_hoverIndex].split(QLatin1Char('\n')).join(QStringLiteral(" "));
        for (const Series& s : m_series)
            lines << s.name + QStringLiteral(": ") + fmtY(s.values.value(m_hoverIndex));
        int tw = 0;
        for (const QString& l : lines)
            tw = qMax(tw, tfm.horizontalAdvance(l));
        tw += 20;
        const int lineH = tfm.height() + 3;
        const int th = int(lines.size()) * lineH + 12;

        int tx = int(xh) + 12;
        if (tx + tw > plot.right())
            tx = int(xh) - tw - 12;
        int ty = plot.top() + 6;
        QRect box(tx, ty, tw, th);
        // soft shadow + rounded card, like the chart.js tooltip
        QColor shadow = pal.bg;
        shadow.setAlpha(120);
        p.setPen(Qt::NoPen);
        p.setBrush(shadow);
        p.drawRoundedRect(box.translated(2, 2), 6, 6);
        p.setPen(QPen(pal.chartTipBd, 1));
        p.setBrush(pal.chartTipBg);
        p.drawRoundedRect(box, 6, 6);
        p.setFont(tf);
        for (int i = 0; i < lines.size(); ++i) {
            p.setPen(i == 0 ? pal.chartTipTitle : pal.chartTipBody);
            p.drawText(QPoint(box.left() + 10,
                              box.top() + 6 + tfm.height() / 2 + i * lineH),
                       lines[i]);
        }
    }
}

int MiniChart::hoverIndexAt(const QPoint& pos) const
{
    const int n = m_labels.size();
    if (n == 0)
        return -1;
    // dynamic top padding (multi-series charts have a legend band at the top)
    const QRect plot = plotRect();
    // only react inside the plot area: y-axis labels, legend band and the
    // x-label strip below the axis shouldn't show a crosshair/tooltip
    if (!plot.adjusted(0, -8, 0, 4).contains(pos))
        return -1;
    const int x = pos.x();
    if (m_isBar || n == 1) {
        const double bw = double(plot.width()) / n;
        return qBound(0, int((x - plot.left()) / bw), n - 1);
    }
    const double step = double(plot.width()) / (n - 1);
    return qBound(0, qRound((x - plot.left()) / step), n - 1);
}

void MiniChart::mouseMoveEvent(QMouseEvent* e)
{
    const int idx = hoverIndexAt(e->pos());
    if (idx != m_hoverIndex) {
        m_hoverIndex = idx;
        update();
    }
}

void MiniChart::leaveEvent(QEvent* e)
{
    QWidget::leaveEvent(e);
    if (m_hoverIndex >= 0) {
        m_hoverIndex = -1;
        update();
    }
}
