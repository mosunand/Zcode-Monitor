#pragma once
// UiUtil.h — shared UI primitives: badges, cards, dots, spinner, status maps.

#include <QFrame>
#include <QLabel>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QWidget>

#include "Theme.h"

class QPainter;
class QRect;
class QTableWidget;

class BadgeLabel : public QWidget {
    Q_OBJECT
public:
    // kinds mirror the web .badge.{green|red|yellow|blue|purple|teal|dim}
    explicit BadgeLabel(const QString& text, const QString& kind, QWidget* parent = nullptr);
    void setKind(const QString& kind);
    void setText(const QString& text);
    QString text() const { return m_text; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString m_text;
    QString m_kind;
};

class CardFrame : public QFrame {
    Q_OBJECT
public:
    explicit CardFrame(QWidget* parent = nullptr);
};

class Dot : public QWidget {
    Q_OBJECT
public:
    explicit Dot(int diameter = 8, QWidget* parent = nullptr);
    void setColor(const QColor& c);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QColor m_color;
    int m_d;
};

class SpinnerWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpinnerWidget(QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int m_angle = 0;
};

namespace UiUtil {

// status → badge kind (app.js statusBadge)
QString statusBadgeKind(const QString& status);
// query_source → badge kind (main_turn=blue, subagent=teal, else dim)
QString sourceBadgeKind(const QString& source);
// token-speed tier (0 none,1 red,2 yellow,3 green) → color
QColor speedColor(int tier);
// category name → dot color (cat-dot.{conversation|llm|tool|network|usage|prompt|lifecycle|other})
QColor catColor(const QString& category);
// a muted h2-style section label with optional sub text
QWidget* sectionLabel(const QString& title, const QString& sub = QString());
// simple label helpers
QLabel* label(const QString& text, const QString& cls = QString());
// status text for lists ("N req · N tok" style) — plain helper
QString fmtCount(qint64 n);
// paint a rounded pill (badge / speed chip) for delegates
void paintPill(QPainter* p, const QRect& rect, const QString& text, const QColor& color,
               bool bold = false);

// ── 行悬停表格：悬停时整行高亮（一条横线），对齐 web 的 tr:hover ──
class RowHoverDelegate;

class RowHoverTable : public QTableWidget {
public:
    explicit RowHoverTable(QWidget* parent = nullptr);
    int hoverRow() const { return m_hoverRow; }

protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    int m_hoverRow = -1;
};

class RowHoverDelegate : public QStyledItemDelegate {
public:
    explicit RowHoverDelegate(RowHoverTable* table);
    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

private:
    const RowHoverTable* m_table;
};

// exact pixel height a table needs to show ALL its rows (header + rows +
// frame) — used to turn inner scrollbars into page-level scrolling, matching
// the web layout where the whole page scrolls
int tableContentHeight(QTableWidget* t);
// same, capped so a huge result set can't produce a monster widget
int tableContentHeight(QTableWidget* t, int maxHeight);

} // namespace UiUtil
