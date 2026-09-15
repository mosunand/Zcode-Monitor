// KpiCard.cpp — see KpiCard.h.

#include "KpiCard.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "ui/Theme.h"

KpiCard::KpiCard(const QString& labelText, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(15, 13, 15, 15);
    lay->setSpacing(3);

    m_label = new QLabel(labelText.toUpper());
    m_label->setProperty("cls", "kpi-label");
    m_value = new QLabel(QStringLiteral("—"));
    m_value->setProperty("cls", "value-fg");
    m_delta = new QLabel();
    m_delta->setProperty("cls", "kpi-delta");
    m_delta->setWordWrap(false);
    m_details = new QLabel();
    m_details->setProperty("cls", "kpi-delta");
    m_details->setWordWrap(true);
    m_details->setTextFormat(Qt::RichText);
    m_details->hide();

    lay->addWidget(m_label);
    lay->addSpacing(2);
    lay->addWidget(m_value);
    lay->addSpacing(1);
    lay->addWidget(m_delta);
    lay->addWidget(m_details);
    lay->addStretch(1);
    // extra room at the bottom when a progress bar is shown (paintEvent)
    setMinimumHeight(100);
    setMinimumWidth(178);
}

void KpiCard::setValue(const QString& text, const QColor& color)
{
    m_value->setText(text);
    // explicit color overrides the stylesheet default (fg); invalid → default
    const int pt = m_largeValue ? 30 : 19;
    m_value->setStyleSheet(color.isValid()
        ? QStringLiteral("font-size:%1pt;font-weight:%2;color:%3;")
              .arg(pt).arg(m_largeValue ? 700 : 600).arg(color.name())
        : QString());
}

void KpiCard::setDelta(const QString& text)
{
    m_delta->setText(text);
}

void KpiCard::setLargeValue(bool on)
{
    if (m_largeValue == on)
        return;
    m_largeValue = on;
    if (on)
        setValue(m_value->text()); // re-apply the 30pt style
    else
        m_value->setStyleSheet(QString()); // back to cls="value-fg" 19pt
}

void KpiCard::setDetails(const QString& html)
{
    m_details->setText(html);
    m_details->setVisible(!html.isEmpty());
}

void KpiCard::setCaption(const QString& text)
{
    m_label->setText(text.toUpper());
}

void KpiCard::setBar(int pct, const QColor& color)
{
    m_barPct = pct;
    m_barColor = color;
    update();
}

void KpiCard::paintEvent(QPaintEvent* e)
{
    QFrame::paintEvent(e);
    if (m_barPct < 0 || !m_barColor.isValid())
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // pinned to the card bottom; the layout keeps the delta line above it
    const QRect track(15, height() - 12, width() - 30, 3);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::instance().pal().surface3);
    p.drawRoundedRect(track, 1.5, 1.5);
    if (m_barPct > 0) {
        p.setBrush(m_barColor);
        QRect fill = track;
        fill.setWidth(qMax(3, int(track.width() * qMin(100, m_barPct)) / 100));
        p.drawRoundedRect(fill, 1.5, 1.5);
    }
}
