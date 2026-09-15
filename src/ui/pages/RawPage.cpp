// RawPage.cpp — see RawPage.h.

#include "RawPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "ui/Theme.h"
#include "ui/UiUtil.h"
#include "core/RawService.h"
#include "util/Async.h"
#include "util/Format.h"

using namespace UiUtil;

namespace {

QString prettyJsonOrRaw(const QString& s)
{
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(s.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && !d.isNull())
        return QString::fromUtf8(d.toJson(QJsonDocument::Indented));
    return s;
}

void showTextDialog(QWidget* parent, const QString& title, const QString& text)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.resize(680, 520);
    auto* lay = new QVBoxLayout(&dlg);
    auto* edit = new QPlainTextEdit(&dlg);
    edit->setPlainText(text);
    edit->setReadOnly(true);
    edit->setFont(QFont(QStringLiteral("Consolas"), 9));
    lay->addWidget(edit);
    auto* close = new QPushButton(QObject::tr("关闭"), &dlg);
    QObject::connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    lay->addWidget(close);
    dlg.exec();
}

} // namespace

RawPage::RawPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    // inline styles bake in palette colors — reload on theme flips
    connect(&Theme::instance(), &Theme::changed, this, [this](bool) {
        if (m_loaded)
            load();
    });
}

void RawPage::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    content->setMaximumWidth(1400);
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(24, 20, 24, 64);
    lay->setSpacing(8);
    auto* center = new QHBoxLayout;
    center->addStretch(1);
    center->addWidget(content, 10);
    center->addStretch(1);
    auto* centerHost = new QWidget;
    centerHost->setLayout(center);
    scroll->setWidget(centerHost);
    outer->addWidget(scroll);

    // toolbar
    auto* toolbar = new QWidget;
    auto* tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(0, 0, 0, 12);
    tb->setSpacing(8);
    auto* h1 = new QLabel(QStringLiteral("原始数据"));
    h1->setObjectName(QStringLiteral("h1"));
    tb->addWidget(h1);
    tb->addStretch(1);

    m_tableCombo = new QComboBox;
    for (const QString& t : RawService::allowedTables())
        m_tableCombo->addItem(t);
    m_tableCombo->setCurrentText(QStringLiteral("session"));
    tb->addWidget(m_tableCombo);

    m_orderCombo = new QComboBox;
    m_orderCombo->addItem(QStringLiteral("不排序"), QString());
    for (const QString& c : RawService::allowedOrderColumns())
        m_orderCombo->addItem(c, c);
    tb->addWidget(m_orderCombo);

    m_descCheck = new QCheckBox(QStringLiteral("降序"));
    tb->addWidget(m_descCheck);

    m_whereInput = new QLineEdit;
    m_whereInput->setPlaceholderText(QStringLiteral("where 条件，如 status='error'"));
    m_whereInput->setFixedWidth(260);
    connect(m_whereInput, &QLineEdit::returnPressed, this, &RawPage::load);
    tb->addWidget(m_whereInput);

    m_goBtn = new QPushButton(QStringLiteral("查询"));
    connect(m_goBtn, &QPushButton::clicked, this, &RawPage::load);
    tb->addWidget(m_goBtn);
    lay->addWidget(toolbar);

    m_note = new QLabel(QStringLiteral("只读访问。最多返回 1000 行。JSON 列双击可展开查看。"));
    m_note->setProperty("cls", "faint");
    m_note->setStyleSheet("font-size:8.5pt;margin-bottom:10px;");
    lay->addWidget(m_note);

    m_count = new QLabel;
    m_count->setProperty("cls", "faint");
    m_count->setStyleSheet("font-size:8.5pt;margin-bottom:8px;");
    lay->addWidget(m_count);

    m_table = new RowHoverTable;
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (row < 0 || col < 0)
            return;
        const QTableWidgetItem* it = m_table->item(row, col);
        if (!it)
            return;
        const QString cellText = it->data(Qt::DisplayRole).toString();
        const bool isJson = cellText.startsWith(QLatin1Char('{'))
            || cellText.startsWith(QLatin1Char('['));
        if (isJson) {
            // pretty-print the JSON cell
            const QString raw = it->data(Qt::UserRole + 1).toString();
            showTextDialog(this, it->data(Qt::UserRole).toString(),
                           prettyJsonOrRaw(raw.isEmpty() ? cellText : raw));
        } else {
            // show the whole row as JSON
            const QVariantMap rowMap = it->data(Qt::UserRole + 2).toMap();
            if (!rowMap.isEmpty()) {
                QJsonObject o;
                for (auto k = rowMap.constBegin(); k != rowMap.constEnd(); ++k) {
                    const QVariant v = k.value();
                    if (v.isNull())
                        continue;
                    if (v.typeId() != QMetaType::QString && v.canConvert<double>())
                        o.insert(k.key(), v.toDouble());
                    else
                        o.insert(k.key(), v.toString());
                }
                showTextDialog(this, QStringLiteral("行 JSON"),
                               QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented)));
            }
        }
    });
    auto* card = new CardFrame;
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(m_table);
    lay->addWidget(card, 1);
}

void RawPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (!m_loaded)
        load();
}

void RawPage::load()
{
    const QString table = m_tableCombo->currentText();
    const QString order = m_orderCombo->currentData().toString();
    const QString where = m_whereInput->text().trimmed();

    // raw queries (esp. with a free-text where) can scan a lot — off-thread
    const int seq = ++m_loadSeq;
    const bool desc = m_descCheck->isChecked();
    Async::run<types::RawResult>(
        this,
        [table, order, where, desc]() {
            return RawService::query(table, 100, 0, order, desc, where);
        },
        [this, seq](const types::RawResult& r) {
            if (seq != m_loadSeq)
                return; // a newer query superseded this one
            m_loaded = true;
            applyResult(r);
        });
}

void RawPage::applyResult(const types::RawResult& r)
{
    if (!r.ok) {
        m_count->setText(r.error);
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        return;
    }
    m_count->setText(QStringLiteral("%1 行（显示前 %2）· 表 %3")
                         .arg(Format::fmtInt64(r.count))
                         .arg(r.rows.size())
                         .arg(r.table));
    if (r.rows.isEmpty()) {
        m_count->setText(QStringLiteral("无数据"));
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        return;
    }

    const QStringList cols = r.columns;
    m_table->setColumnCount(cols.size());
    m_table->setHorizontalHeaderLabels(cols);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setRowCount(int(r.rows.size()));
    const QFont mono(QStringLiteral("Consolas"), 8);
    for (int i = 0; i < r.rows.size(); ++i) {
        const QVariantMap& row = r.rows.at(i);
        for (int c = 0; c < cols.size(); ++c) {
            const QVariant v = row.value(cols.at(c));
            QString text;
            bool numeric = false;
            if (v.isNull()) {
                text = QStringLiteral("null");
            } else if (v.typeId() != QMetaType::QString
                       && v.typeId() != QMetaType::QByteArray
                       && v.canConvert<qint64>()) {
                text = QString::number(v.toLongLong());
                numeric = true;
            } else {
                text = v.toString();
            }
            bool isJson = text.startsWith(QLatin1Char('{')) || text.startsWith(QLatin1Char('['));
            QString display = text;
            if (display.size() > 60)
                display = display.left(60) + QStringLiteral("…");
            if (isJson)
                display = QStringLiteral("{…}");
            auto* it = new QTableWidgetItem(display);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            it->setFont(mono);
            if (numeric) {
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                it->setData(Qt::UserRole + 3, true);
            }
            if (v.isNull())
                it->setForeground(Theme::instance().pal().fg4);
            else if (isJson)
                it->setForeground(Theme::instance().pal().catTool2);
            it->setData(Qt::UserRole, cols.at(c)); // column name for the dialog title
            it->setData(Qt::UserRole + 1, text);   // full cell text
            it->setData(Qt::UserRole + 2, row);    // whole row map
            it->setToolTip(text.left(400));
            m_table->setItem(i, c, it);
        }
    }
    m_table->resizeColumnsToContents();
}
