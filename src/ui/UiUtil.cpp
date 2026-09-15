// UiUtil.cpp — see UiUtil.h.

#include "UiUtil.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVariant>

// ── BadgeLabel ────────────────────────────────────────────────

BadgeLabel::BadgeLabel(const QString& text, const QString& kind, QWidget* parent)
    : QWidget(parent)
    , m_text(text)
    , m_kind(kind)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // avoid the QSS QWidget background painting under the translucent pill
    setAttribute(Qt::WA_NoSystemBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void BadgeLabel::setKind(const QString& kind)
{
    m_kind = kind;
    update();
}

void BadgeLabel::setText(const QString& text)
{
    m_text = text;
    updateGeometry();
    update();
}

QSize BadgeLabel::sizeHint() const
{
    QFont f = font();
    f.setPointSizeF(8.0);
    const int w = QFontMetrics(f).horizontalAdvance(m_text);
    return QSize(w + 16, QFontMetrics(f).height() + 5);
}

void BadgeLabel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    f.setPointSizeF(8.0);
    p.setFont(f);

    const QColor color = Theme::instance().badgeColor(m_kind);
    QColor border = color;
    border.setAlpha(qRound(color.alpha() * 0.30));
    QColor bg = color;
    bg.setAlpha(qRound(color.alpha() * 0.08));

    p.setPen(QPen(border, 1));
    p.setBrush(bg);
    // capsule shape, but cap the radius so one-line badges stay subtle
    const qreal r = qMin<qreal>(9.0, qMin(width(), height()) / 2.0);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), r, r);
    p.setPen(color);
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

// ── CardFrame ────────────────────────────────────────────────

CardFrame::CardFrame(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
}

// ── Dot ──────────────────────────────────────────────────────

Dot::Dot(int diameter, QWidget* parent)
    : QWidget(parent)
    , m_d(diameter)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // don't let the QWidget background-color (from QSS) paint under us —
    // it tints the glow and dulls the core color
    setAttribute(Qt::WA_NoSystemBackground);
    setFixedSize(diameter + 2, diameter + 2);
}

void Dot::setColor(const QColor& c)
{
    m_color = c;
    update();
}

QSize Dot::sizeHint() const
{
    return QSize(m_d + 2, m_d + 2);
}

void Dot::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    // soft glow (web .dot box-shadow), then the solid core
    QColor glow = m_color;
    glow.setAlpha(70);
    p.setBrush(glow);
    p.drawEllipse(0, 0, m_d + 2, m_d + 2);
    // defensive: force full opacity even if the palette color carried alpha
    QColor core = m_color;
    core.setAlpha(255);
    p.setBrush(core);
    p.drawEllipse(1, 1, m_d, m_d);
}

// ── SpinnerWidget ─────────────────────────────────────────────

SpinnerWidget::SpinnerWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(15, 15);
    auto* t = new QTimer(this);
    connect(t, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + 30) % 360;
        update();
    });
    t->start(60);
}

QSize SpinnerWidget::sizeHint() const
{
    return QSize(15, 15);
}

void SpinnerWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPen pen(Theme::instance().pal().accent, 2);
    p.setPen(pen);
    p.drawArc(QRect(2, 2, width() - 5, height() - 5), m_angle * 16, 300 * 16);
}

// ── helpers ──────────────────────────────────────────────────

namespace UiUtil {

QString statusBadgeKind(const QString& status)
{
    if (status == QLatin1String("completed") || status == QLatin1String("success"))
        return QStringLiteral("green");
    if (status == QLatin1String("running"))
        return QStringLiteral("blue");
    if (status == QLatin1String("error") || status == QLatin1String("failed"))
        return QStringLiteral("red");
    if (status == QLatin1String("cancelled"))
        return QStringLiteral("yellow");
    return QStringLiteral("dim");
}

QString sourceBadgeKind(const QString& source)
{
    if (source == QLatin1String("main_turn"))
        return QStringLiteral("blue");
    if (source == QLatin1String("subagent"))
        return QStringLiteral("teal");
    return QStringLiteral("dim");
}

QColor speedColor(int tier)
{
    const Palette& p = Theme::instance().pal();
    switch (tier) {
    case 1: return p.sevErr;
    case 2: return p.sevWarn;
    case 3: return p.sevOk;
    default: return p.fg4;
    }
}

QColor catColor(const QString& category)
{
    const Palette& p = Theme::instance().pal();
    if (category == QLatin1String("conversation")) return p.catConversation;
    if (category == QLatin1String("llm")) return p.catLlm;
    if (category == QLatin1String("tool")) return p.catTool;
    if (category == QLatin1String("network")) return p.catNetwork;
    if (category == QLatin1String("usage")) return p.catUsage;
    if (category == QLatin1String("prompt")) return p.catPrompt;
    if (category == QLatin1String("lifecycle")) return p.catLifecycle;
    return p.fg5; // other
}

QWidget* sectionLabel(const QString& title, const QString& sub)
{
    auto* w = new QWidget;
    w->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 14, 0, 8);
    lay->setSpacing(8);
    auto* t = new QLabel(title);
    t->setObjectName(QStringLiteral("h2"));
    lay->addWidget(t);
    if (!sub.isEmpty()) {
        auto* s = new QLabel(sub);
        s->setProperty("cls", "sectionSub");
        lay->addWidget(s, 1);
    } else {
        lay->addStretch(1);
    }
    return w;
}

QLabel* label(const QString& text, const QString& cls)
{
    auto* l = new QLabel(text);
    if (!cls.isEmpty())
        l->setProperty("cls", cls);
    return l;
}

QString fmtCount(qint64 n)
{
    return QString::number(n);
}

void paintPill(QPainter* p, const QRect& rect, const QString& text, const QColor& color,
               bool bold)
{
    QColor border = color;
    border.setAlpha(qRound(color.alpha() * 0.35));
    QColor bg = color;
    bg.setAlpha(qRound(color.alpha() * 0.10));
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QFont f = p->font();
    f.setPointSizeF(8.5);
    f.setBold(bold);
    f.setFamily(QStringLiteral("Consolas"));
    p->setFont(f);
    const qreal r = qMin<qreal>(8.0, rect.height() / 2.0);
    p->setPen(QPen(border, 1));
    p->setBrush(bg);
    p->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
    p->setPen(color);
    p->setBrush(Qt::NoBrush);
    p->drawText(rect, Qt::AlignCenter, text);
    p->restore();
}

} // namespace

// ── RowHoverTable / RowHoverDelegate ─────────────────────────

UiUtil::RowHoverTable::RowHoverTable(QWidget* parent)
    : QTableWidget(parent)
{
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    viewport()->installEventFilter(this);
    setItemDelegate(new RowHoverDelegate(this));
}

bool UiUtil::RowHoverTable::eventFilter(QObject* watched, QEvent* e)
{
    if (watched == viewport()) {
        if (e->type() == QEvent::MouseMove) {
            const int row = rowAt(static_cast<QMouseEvent*>(e)->pos().y());
            if (row != m_hoverRow) {
                m_hoverRow = row;
                viewport()->update();
            }
        } else if (e->type() == QEvent::Leave) {
            if (m_hoverRow != -1) {
                m_hoverRow = -1;
                viewport()->update();
            }
        }
    }
    return QTableWidget::eventFilter(watched, e);
}

UiUtil::RowHoverDelegate::RowHoverDelegate(RowHoverTable* table)
    : QStyledItemDelegate(table)
    , m_table(table)
{
}

void UiUtil::RowHoverDelegate::paint(QPainter* p, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
    // 整行横线高亮：该行的每个 cell 都铺上 hover 底色，连成一条横线
    if (m_table && index.row() == m_table->hoverRow()) {
        p->fillRect(option.rect, Theme::instance().pal().surface2);
    }
    QStyledItemDelegate::paint(p, option, index);
}

int UiUtil::tableContentHeight(QTableWidget* t)
{
    int h = t->horizontalHeader()->height();
    for (int r = 0; r < t->rowCount(); ++r)
        h += t->rowHeight(r);
    h += 2 * t->frameWidth() + 1;
    return h;
}

int UiUtil::tableContentHeight(QTableWidget* t, int maxHeight)
{
    return qMin(tableContentHeight(t), maxHeight);
}
