// LiveFeed.cpp — see LiveFeed.h.

#include "LiveFeed.h"

#include <QDateTime>
#include <QPainter>

#include "ui/Theme.h"
#include "ui/UiUtil.h"
#include "util/Format.h"

using namespace UiUtil;

// ── model ────────────────────────────────────────────────────

LiveFeedModel::LiveFeedModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void LiveFeedModel::pushModelRows(const QVector<types::ModelRow>& rows)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVector<FeedRow> feed;
    for (const types::ModelRow& r : rows) {
        FeedRow f;
        f.isModel = true;
        f.cat = QStringLiteral("llm");
        f.label = QStringLiteral("llm→");
        QString variant;
        if (!r.variant.isEmpty())
            variant = r.variant;
        f.summary = QStringLiteral("%1 · %2 %3 · in %4 / out %5")
                        .arg(r.querySource, r.modelId.isEmpty() ? QStringLiteral("?") : r.modelId,
                             variant,
                             Format::fmtNum(double(r.inputTokens)),
                             Format::fmtNum(double(r.outputTokens)));
        if (r.reasoningTokens > 0)
            f.summary += QStringLiteral(" / think ") + Format::fmtNum(double(r.reasoningTokens));
        if (r.status == QLatin1String("completed") && r.durationMs > 0) {
            f.spdOk = true;
            f.spd = double(r.outputTokens + r.reasoningTokens) / (double(r.durationMs) / 1000.0);
        }
        f.status = r.status;
        f.sid = r.sessionId;
        f.durOk = r.durationMs > 0;
        f.durMs = r.durationMs;
        f.timeMs = r.startedMs;
        f.addedAtMs = now;
        feed.push_back(f);
    }
    if (feed.isEmpty())
        return;
    beginInsertRows(QModelIndex(), 0, feed.size() - 1);
    // rows arrive oldest → newest; seq increases with time, list is
    // newest-first, so prepend in order and number the batch backwards
    for (int i = 0; i < feed.size(); ++i)
        feed[i].seq = m_seq + qint64(feed.size() - 1 - i);
    m_seq += feed.size();
    for (int i = feed.size() - 1; i >= 0; --i)
        m_rows.push_front(feed[i]);
    endInsertRows();
    while (m_rows.size() > 60) {
        beginRemoveRows(QModelIndex(), m_rows.size() - 1, m_rows.size() - 1);
        m_rows.removeLast();
        endRemoveRows();
    }
}

void LiveFeedModel::pushToolRows(const QVector<types::ToolRow>& rows)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVector<FeedRow> feed;
    for (const types::ToolRow& r : rows) {
        FeedRow f;
        f.isModel = false;
        f.cat = QStringLiteral("tool");
        f.label = QStringLiteral("tool.call");
        f.summary = QStringLiteral("%1 · %2").arg(r.toolName, r.status);
        if (r.hasExit)
            f.summary += QStringLiteral(" exit=") + QString::number(r.exitCode);
        f.status = r.status;
        f.sid = r.sessionId;
        f.durOk = r.durationMs > 0;
        f.durMs = r.durationMs;
        f.timeMs = r.startedMs;
        f.addedAtMs = now;
        feed.push_back(f);
    }
    if (feed.isEmpty())
        return;
    beginInsertRows(QModelIndex(), 0, feed.size() - 1);
    // rows arrive oldest → newest; seq increases with time, list is
    // newest-first, so prepend in order and number the batch backwards
    for (int i = 0; i < feed.size(); ++i)
        feed[i].seq = m_seq + qint64(feed.size() - 1 - i);
    m_seq += feed.size();
    for (int i = feed.size() - 1; i >= 0; --i)
        m_rows.push_front(feed[i]);
    endInsertRows();
    while (m_rows.size() > 60) {
        beginRemoveRows(QModelIndex(), m_rows.size() - 1, m_rows.size() - 1);
        m_rows.removeLast();
        endRemoveRows();
    }
}

void LiveFeedModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

int LiveFeedModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant LiveFeedModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    if (role == Qt::UserRole + 1)
        return QVariant::fromValue(index.row());
    return {};
}

// ── delegate ──────────────────────────────────────────────────

LiveFeedDelegate::LiveFeedDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QSize LiveFeedDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return QSize(200, 38);
}

void LiveFeedDelegate::paint(QPainter* p, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
    const auto* feed = qobject_cast<const LiveFeedModel*>(index.model());
    if (!feed)
        return;
    // guard against a paint on a stale index right after row removal
    // (rowAt() is a plain QVector::at — no bounds check in release builds)
    if (index.row() < 0 || index.row() >= feed->rowCount())
        return;
    const FeedRow& r = feed->rowAt(index.row());
    const Palette& pal = Theme::instance().pal();
    const QRect rect = option.rect;

    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    // backgrounds: flash (newest) > error tint > hover/selected
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - r.addedAtMs;
    if (age < 1200) {
        QColor flash = pal.accent;
        flash.setAlpha(qRound(0.18 * 255 * (1.0 - double(age) / 1200.0)));
        p->fillRect(rect, flash);
    } else if (r.status == QLatin1String("error")) {
        QColor tint = pal.sevErr;
        tint.setAlpha(18);
        p->fillRect(rect, tint);
    } else if (option.state & QStyle::State_MouseOver) {
        p->fillRect(rect, pal.surface1);
    }
    p->setPen(QPen(pal.borderSoft, 1));
    p->drawLine(rect.left(), rect.bottom(), rect.right(), rect.bottom());

    QFont mono(QStringLiteral("Consolas"));
    QFont smallF = mono;
    smallF.setPointSizeF(8.5);
    QFont bodyF = mono;
    bodyF.setPointSizeF(9.5);
    QFont labelF = mono;
    labelF.setPointSizeF(9.5);
    labelF.setBold(true);
    QFontMetrics sm(smallF);
    QFontMetrics bm(bodyF);
    QFontMetrics lfm(labelF);

    // seq + time
    p->setFont(smallF);
    p->setPen(pal.fg5);
    p->drawText(QRect(rect.left() + 8, rect.y(), 42, rect.height()),
                Qt::AlignRight | Qt::AlignVCenter, QString::number(r.seq));
    p->setPen(pal.fg4);
    p->drawText(QRect(rect.left() + 56, rect.y(), 84, rect.height()),
                Qt::AlignLeft | Qt::AlignVCenter, Format::fmtTime(r.timeMs));

    // category dot
    p->setPen(Qt::NoPen);
    p->setBrush(catColor(r.cat));
    p->drawEllipse(QPoint(rect.left() + 152, rect.center().y()), 4, 4);

    // ── right block: 从右往左排（徽章 → 时长 → 速度 chip），绝不与描述重叠 ──
    int rightEdge = rect.right() - 12;

    // status badge
    const QString statusText = r.status;
    const QColor stColor = Theme::instance().badgeColor(statusBadgeKind(r.status));
    const int badgeW = sm.horizontalAdvance(statusText) + 18;
    const int badgeX = rightEdge - badgeW;
    paintPill(p, QRect(badgeX, rect.center().y() - 9, badgeW, 18), statusText, stColor);
    rightEdge = badgeX - 12;

    // duration
    const QString durText = r.durOk ? Format::fmtMs(double(r.durMs)) : QString();
    if (!durText.isEmpty()) {
        const int dw = sm.horizontalAdvance(durText);
        p->setFont(smallF);
        p->setPen(pal.fg4);
        p->drawText(QRect(rightEdge - dw, rect.y(), dw + 4, rect.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, durText);
        rightEdge -= dw + 12;
    }

    // speed chip
    if (r.spdOk) {
        const QString chipText = QString::number(r.spd, 'f', 1) + QStringLiteral(" t/s");
        const QColor chipColor = UiUtil::speedColor(Format::speedTier(r.spd));
        const int chipW = sm.horizontalAdvance(chipText) + 18;
        paintPill(p, QRect(rightEdge - chipW, rect.center().y() - 9, chipW, 18),
                  chipText, chipColor);
        rightEdge -= chipW + 12;
    }

    // ── desc: label（彩色粗体）+ summary（省略）+ 短 id，全部限制在 descEnd 之内 ──
    const int descX = rect.left() + 168;
    int x = descX;
    const QString labelText = r.label;
    const QColor labelColor = (r.cat == QLatin1String("llm")) ? pal.catLlm2 : pal.catTool;
    p->setFont(labelF);
    p->setPen(labelColor);
    p->drawText(QRect(x, rect.y(), lfm.horizontalAdvance(labelText) + 2, rect.height()),
                Qt::AlignLeft | Qt::AlignVCenter, labelText);
    x += lfm.horizontalAdvance(labelText) + 8;

    const QString sidText = r.sid.left(8);
    const int sidW = bm.horizontalAdvance(sidText) + 8;
    const int avail = qMax(40, (rightEdge - 4) - x - sidW - 6);
    const QString summary = bm.elidedText(r.summary, Qt::ElideRight, avail);
    p->setFont(bodyF);
    p->setPen(pal.fg1);
    p->drawText(QRect(x, rect.y(), bm.horizontalAdvance(summary) + 4, rect.height()),
                Qt::AlignLeft | Qt::AlignVCenter, summary);
    x += bm.horizontalAdvance(summary) + 8;

    p->setFont(bodyF);
    p->setPen(pal.fg4);
    p->drawText(QRect(x, rect.y(), sidW, rect.height()),
                Qt::AlignLeft | Qt::AlignVCenter, sidText);

    p->restore();
}

// ── view ─────────────────────────────────────────────────────

LiveFeed::LiveFeed(QWidget* parent)
    : QListView(parent)
    , m_model(new LiveFeedModel(this))
{
    setModel(m_model);
    setItemDelegate(new LiveFeedDelegate(this));
    setMouseTracking(true);
    setSelectionMode(QAbstractItemView::NoSelection);
    setFrameShape(QFrame::NoFrame);
    viewport()->setAttribute(Qt::WA_Hover);

    // empty-state placeholder (web: "等待新事件…") — color via cls property
    // so theme flips restyle it, geometry tracked on resize
    m_emptyLabel = new QLabel(QStringLiteral("等待新事件…"), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setProperty("cls", "faint");
    m_emptyLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto updateEmpty = [this] {
        m_emptyLabel->setVisible(m_model->rowCount() == 0);
        m_emptyLabel->setGeometry(0, 0, width(), qMax(height(), 60));
    };
    updateEmpty();
    connect(m_model, &LiveFeedModel::rowsInserted, this, updateEmpty);
    connect(m_model, &LiveFeedModel::rowsRemoved, this, updateEmpty);
    connect(m_model, &LiveFeedModel::modelReset, this, updateEmpty);

    m_flashTimer.setInterval(100);
    connect(&m_flashTimer, &QTimer::timeout, this, [this] {
        viewport()->update();
        // stop once nothing is hot anymore
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int i = 0; i < m_model->rowCount() && i < 4; ++i) {
            if (now - m_model->rowAt(i).addedAtMs < 1200)
                return;
        }
        m_flashTimer.stop();
    });
    connect(m_model, &LiveFeedModel::rowsInserted, this,
            [this] { m_flashTimer.start(); });
}

void LiveFeed::resizeEvent(QResizeEvent* e)
{
    QListView::resizeEvent(e);
    if (m_emptyLabel)
        m_emptyLabel->setGeometry(0, 0, width(), qMax(height(), 60));
}
