// AgentsPage.cpp — see AgentsPage.h.

#include "AgentsPage.h"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/Theme.h"
#include "ui/UiUtil.h"
#include "core/DbService.h"
#include "util/Async.h"
#include "util/Format.h"

using namespace UiUtil;

AgentsPage::AgentsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(24, 20, 24, 64);
    lay->setSpacing(6);

    // inline styles bake in palette colors — reload on theme flips
    connect(&Theme::instance(), &Theme::changed, this, [this](bool) {
        if (m_loaded)
            load();
    });

    auto* h1 = new QLabel(QStringLiteral("子 Agent 关系树"));
    h1->setObjectName(QStringLiteral("h1"));
    lay->addWidget(h1);
    auto* sub = new QLabel(QStringLiteral(
        "主会话 → 派生的子 agent（parent_id 级联）。点击节点跳转会话详情。"));
    sub->setProperty("cls", "muted");
    sub->setStyleSheet("font-size:9pt;");
    lay->addWidget(sub);

    m_summary = new QLabel;
    m_summary->setProperty("cls", "faint");
    m_summary->setStyleSheet("font-size:8.5pt;margin-top:8px;");
    lay->addWidget(m_summary);

    m_tree = new QTreeWidget;
    // single-click a row toggles its subtree (web .row onclick)
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int col) {
        Q_UNUSED(col);
        if (item->childCount() > 0)
            item->setExpanded(!item->isExpanded());
    });
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QStringLiteral("会话"), QStringLiteral("统计"),
                             QStringLiteral("更新"), QStringLiteral("")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(16);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // double-click / Enter opens the session timeline
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                const QString id = item->data(0, Qt::UserRole).toString();
                if (!id.isEmpty())
                    emit openSessionRequested(id, QStringLiteral("timeline"));
            });
    lay->addWidget(m_tree);
    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void AgentsPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (!m_loaded)
        load();
}

void AgentsPage::load()
{
    // the forest query + its per-session sub-queries — off the GUI thread
    const int seq = ++m_loadSeq;
    Async::run<QVector<types::AgentNode>>(
        this,
        []() { return DbService::instance().agentsForest(); },
        [this, seq](const QVector<types::AgentNode>& forest) {
            if (seq != m_loadSeq)
                return; // superseded
            applyForest(forest);
        });
}

void AgentsPage::applyForest(const QVector<types::AgentNode>& forest)
{
    int total = 0;
    std::function<int(const QVector<types::AgentNode>&)> count =
        [&](const QVector<types::AgentNode>& nodes) -> int {
        int n = int(nodes.size());
        for (const types::AgentNode& x : nodes)
            n += count(x.children);
        return n;
    };
    total = count(forest);
    m_summary->setText(QStringLiteral("%1 个会话 · %2 个根会话").arg(total).arg(forest.size()));
    m_loaded = true;

    const Palette& pal = Theme::instance().pal();
    m_tree->clear();

    std::function<void(const QVector<types::AgentNode>&, QTreeWidgetItem*, int)> addNodes =
        [&](const QVector<types::AgentNode>& nodes, QTreeWidgetItem* parent, int depth) {
        for (const types::AgentNode& n : nodes) {
            auto* item = new QTreeWidgetItem(parent);
            const bool isMain = n.parentId.isEmpty();
            const QString label = n.title.isEmpty()
                ? (isMain ? QStringLiteral("(无标题)") : n.id) : n.title;
            // web: ● title (accent) for main / └ title (teal) for children, indent by depth
            item->setText(0, QString(depth * 2, QChar(' '))
                                + (isMain ? QStringLiteral("● ")
                                          : QStringLiteral("└ "))
                                + label);
            item->setForeground(0, isMain ? pal.accent : pal.catTool2);
            item->setData(0, Qt::UserRole, n.id);
            item->setToolTip(0, n.id);
            item->setText(1, QStringLiteral("%1req · %2tools · %3tok")
                                 .arg(Format::fmtInt64(n.modelCalls))
                                 .arg(Format::fmtInt64(n.toolCalls))
                                 .arg(Format::fmtNum(double(n.tokens))));
            item->setForeground(1, pal.fg4);
            item->setText(2, Format::relTime(n.timeUpdated));
            item->setForeground(2, pal.fg4);
            auto* openBtn = new QPushButton(QStringLiteral("打开 →"));
            openBtn->setProperty("cls", "ghost");
            openBtn->setCursor(Qt::PointingHandCursor);
            openBtn->setFlat(true);
            const QString id = n.id;
            connect(openBtn, &QPushButton::clicked, this, [this, id]() {
                emit openSessionRequested(id, QStringLiteral("timeline"));
            });
            m_tree->setItemWidget(item, 3, openBtn);
            if (parent == nullptr)
                m_tree->addTopLevelItem(item);
            if (!n.children.isEmpty())
                addNodes(n.children, item, depth + 1);
        }
    };
    addNodes(forest, nullptr, 0);
    m_tree->expandToDepth(0);
}
