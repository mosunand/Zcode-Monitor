// SessionsPage.cpp — see SessionsPage.h.

#include "SessionsPage.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedLayout>
#include <QStackedLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <map>

#include "ContextTab.h"
#include "TimelineTab.h"
#include "ui/Theme.h"
#include "ui/UiUtil.h"
#include "core/DbService.h"
#include "util/Async.h"
#include "util/Format.h"

using namespace UiUtil;

namespace {

// one session row in the left list (web .listitem). Colors come from
// QSS dynamic properties so theme flips re-color them for free.
class SessionItemWidget : public QWidget {
public:
    SessionItemWidget(const types::SessionRow& s, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(12, 9, 12, 9);
        lay->setSpacing(3);

        auto* title = new QLabel(s.title.isEmpty() ? QStringLiteral("(无标题)") : s.title);
        title->setStyleSheet(QStringLiteral("font-size:9.5pt;"));
        lay->addWidget(title);

        auto* sub = new QWidget;
        auto* sl = new QHBoxLayout(sub);
        sl->setContentsMargins(0, 0, 0, 0);
        sl->setSpacing(8);
        auto* time = new QLabel(Format::relTime(s.timeUpdated));
        time->setProperty("cls", "mono-faint");
        sl->addWidget(time);
        const bool isSub = s.taskType == QLatin1String("subagent_child");
        sl->addWidget(new BadgeLabel(isSub ? QStringLiteral("subagent") : QStringLiteral("main"),
                                     isSub ? QStringLiteral("dim") : QStringLiteral("blue")));
        if (s.totalTokensOk && s.totalTokens > 0) {
            auto* tok = new QLabel(Format::fmtNum(double(s.totalTokens)) + QStringLiteral(" tok"));
            tok->setProperty("cls", "mono-faint");
            sl->addWidget(tok);
        }
        if (s.modelCalls > 0) {
            auto* req = new QLabel(Format::fmtInt(double(s.modelCalls)) + QStringLiteral(" req"));
            req->setProperty("cls", "mono-faint");
            sl->addWidget(req);
        }
        sl->addStretch(1);
        lay->addWidget(sub);
    }
};

// horizontal duration bar for a turn (web .turn .bar)
class TurnBar : public QWidget {
public:
    TurnBar(int pct, const QColor& color, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_pct(pct)
        , m_color(color)
    {
        setAttribute(Qt::WA_NoSystemBackground); // no QSS background under the bar
        setFixedHeight(5);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::instance().pal().surface3);
        p.drawRoundedRect(rect(), 2, 2);
        if (m_pct > 0) {
            p.setBrush(m_color);
            QRect fill = rect();
            fill.setWidth(qMax(2, int(rect().width() * m_pct) / 100));
            p.drawRoundedRect(fill, 2, 2);
        }
    }

private:
    int m_pct;
    QColor m_color;
};

QTableWidgetItem* cellNum(const QString& text)
{
    auto* it = new QTableWidgetItem(text);
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    it->setFlags(Qt::ItemIsEnabled);
    return it;
}

QTableWidgetItem* cellTxt(const QString& text)
{
    auto* it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled);
    return it;
}

QString todoStatusKind(const QString& status)
{
    if (status == QLatin1String("pending"))
        return QStringLiteral("yellow");
    if (status == QLatin1String("in_progress"))
        return QStringLiteral("blue");
    if (status == QLatin1String("completed"))
        return QStringLiteral("green");
    return QStringLiteral("dim");
}

QString todoPriorityKind(const QString& priority)
{
    if (priority == QLatin1String("high"))
        return QStringLiteral("red");
    if (priority == QLatin1String("medium"))
        return QStringLiteral("yellow");
    if (priority == QLatin1String("low"))
        return QStringLiteral("dim");
    return QStringLiteral("dim");
}

} // namespace

SessionsPage::SessionsPage(QWidget* parent)
    : QWidget(parent)
    , m_currentTab(QStringLiteral("timeline"))
{
    buildUi();
    // Re-render on theme flips. Context/Timeline tabs own their theme
    // handlers (with expand-state / filter preservation), so we must NOT
    // loadTab() here — a second load would reset both tab states.
    connect(&Theme::instance(), &Theme::changed, this, [this](bool) {
        renderList(); // re-color the list items
        if (m_currentId.isEmpty())
            return;
        loadDetailHead(); // async internally
        // re-render only the tabs that don't self-heal on theme changes;
        // each Async branch guards itself against a superseded session
        const QString id = m_currentId;
        if (m_currentTab == QLatin1String("turns") || m_currentTab == QLatin1String("usage")) {
            const int seq = ++m_tabSeq;
            Async::run<QVector<types::TurnRow>>(
                this, [id]() { return DbService::instance().sessionTurns(id); },
                [this, seq, id](const QVector<types::TurnRow>& turns) {
                    if (seq != m_tabSeq || id != m_currentId)
                        return;
                    if (m_currentTab == QLatin1String("turns"))
                        renderTurns(turns);
                    else
                        renderUsage(turns);
                });
        } else if (m_currentTab == QLatin1String("agents")) {
            const int seq = ++m_tabSeq;
            Async::run<QVector<types::ChildAgent>>(
                this, [id]() { return DbService::instance().sessionChildrenEnriched(id); },
                [this, seq, id](const QVector<types::ChildAgent>& children) {
                    if (seq != m_tabSeq || id != m_currentId)
                        return;
                    renderAgents(children);
                });
        } else if (m_currentTab == QLatin1String("tasks")) {
            const int seq = ++m_tabSeq;
            Async::run<QVector<types::TodoRow>>(
                this, [id]() { return DbService::instance().todosForSession(id); },
                [this, seq, id](const QVector<types::TodoRow>& todos) {
                    if (seq != m_tabSeq || id != m_currentId)
                        return;
                    renderTasks(todos);
                });
        } else if (m_currentTab == QLatin1String("state")) {
            const int seq = ++m_tabSeq;
            Async::run<types::SessionDetail>(
                this, [id]() { return DbService::instance().sessionGet(id); },
                [this, seq, id](const types::SessionDetail& detail) {
                    if (seq != m_tabSeq || id != m_currentId)
                        return;
                    renderState(detail);
                });
        }
    });
}

void SessionsPage::buildUi()
{
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);

    // ── left: session list ──
    auto* listpane = new QFrame;
    listpane->setObjectName(QStringLiteral("listpane"));
    listpane->setMinimumWidth(260);
    auto* lp = new QVBoxLayout(listpane);
    lp->setContentsMargins(0, 0, 0, 0);
    lp->setSpacing(8);
    auto* head = new QWidget;
    auto* hl = new QVBoxLayout(head);
    hl->setContentsMargins(12, 10, 12, 10);
    hl->setSpacing(8);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("搜索 id / 标题 / 目录…"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &SessionsPage::renderList);
    hl->addWidget(m_search);
    auto* rowSel = new QWidget;
    auto* rl = new QHBoxLayout(rowSel);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(6);
    m_taskType = new QComboBox;
    m_taskType->addItem(QStringLiteral("全部类型"), QString());
    m_taskType->addItem(QStringLiteral("interactive"), QStringLiteral("interactive"));
    m_taskType->addItem(QStringLiteral("subagent"), QStringLiteral("subagent_child"));
    m_taskType->addItem(QStringLiteral("side_chat"), QStringLiteral("selection_side_chat"));
    connect(m_taskType, &QComboBox::currentIndexChanged, this, &SessionsPage::loadList);
    rl->addWidget(m_taskType, 1);
    m_sort = new QComboBox;
    m_sort->addItem(QStringLiteral("最近"), QStringLiteral("recent"));
    m_sort->addItem(QStringLiteral("token 多"), QStringLiteral("tokens"));
    m_sort->addItem(QStringLiteral("调用多"), QStringLiteral("calls"));
    connect(m_sort, &QComboBox::currentIndexChanged, this, &SessionsPage::renderList);
    rl->addWidget(m_sort, 1);
    hl->addWidget(rowSel);
    lp->addWidget(head);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("sessionList"));
    m_list->setUniformItemSizes(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(m_list, &QListWidget::currentRowChanged, this, &SessionsPage::onListItemClicked);
    lp->addWidget(m_list, 1);
    splitter->addWidget(listpane);

    // ── right: detail ──
    auto* detail = new QWidget;
    auto* dp = new QVBoxLayout(detail);
    dp->setContentsMargins(0, 0, 0, 0);
    dp->setSpacing(0);

    auto* headPane = new QWidget;
    auto* hp = new QVBoxLayout(headPane);
    hp->setContentsMargins(16, 12, 16, 12);
    hp->setSpacing(3);
    auto* titleRow = new QWidget;
    auto* trl = new QHBoxLayout(titleRow);
    trl->setContentsMargins(0, 0, 0, 0);
    trl->setSpacing(8);
    m_title = new QLabel(QStringLiteral("← 从左侧选择一个会话"));
    m_title->setObjectName(QStringLiteral("detailTitle"));
    trl->addWidget(m_title);
    trl->addStretch(1);
    hp->addWidget(titleRow);
    m_titleSub = new QLabel;
    m_titleSub->setObjectName(QStringLiteral("detailSub"));
    m_titleSub->setWordWrap(false);
    hp->addWidget(m_titleSub);
    dp->addWidget(headPane);

    m_tabs = new QTabWidget;
    m_timelineTab = new TimelineTab;
    m_contextTab = new ContextTab;
    m_turnsTab = buildTurnsTab();
    m_agentsTab = buildAgentsTab();
    m_tasksTab = buildTasksTab();
    m_usageTab = buildUsageTab();
    m_stateTab = buildStateTab();
    m_tabs->addTab(m_timelineTab, QStringLiteral("Timeline"));
    m_tabs->addTab(m_contextTab, QStringLiteral("Context"));
    m_tabs->addTab(m_turnsTab, QStringLiteral("Turns"));
    m_tabs->addTab(m_agentsTab, QStringLiteral("Agents"));
    m_tabs->addTab(m_tasksTab, QStringLiteral("Tasks"));
    m_tabs->addTab(m_usageTab, QStringLiteral("Usage"));
    m_tabs->addTab(m_stateTab, QStringLiteral("State"));
    connect(m_tabs, &QTabWidget::currentChanged, this, &SessionsPage::onTabChanged);
    connect(m_timelineTab, &TimelineTab::openSessionRequested, this,
            &SessionsPage::openSessionRequested);
    dp->addWidget(m_tabs, 1);
    // web shows "← 从左侧选择一个会话" inside the tab body when nothing is
    // selected — mirror that with a stacked placeholder
    m_detailStack = new QStackedLayout;
    auto* emptyHost = new QWidget;
    auto* eh = new QVBoxLayout(emptyHost);
    auto* empty = new QLabel(QStringLiteral("← 从左侧选择一个会话"));
    empty->setAlignment(Qt::AlignCenter);
    empty->setStyleSheet(QStringLiteral("color:%1;font-size:10pt;")
                             .arg(Theme::instance().pal().fg4.name()));
    eh->addWidget(empty);
    auto* tabsHost = new QWidget;
    auto* th = new QVBoxLayout(tabsHost);
    th->setContentsMargins(0, 0, 0, 0);
    th->addWidget(m_tabs);
    m_detailStack->addWidget(tabsHost);
    m_detailStack->addWidget(emptyHost);
    m_detailStack->setCurrentIndex(1); // placeholder until a session is picked
    auto* stackHost = new QWidget;
    stackHost->setLayout(m_detailStack);
    dp->removeWidget(m_tabs);
    dp->addWidget(stackHost, 1);

    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 1000});

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(splitter);
}

void SessionsPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (!m_listLoaded)
        loadList();
}

void SessionsPage::loadList()
{
    // 500 sessions × 3 correlated sub-queries — off the GUI thread
    const int seq = ++m_listSeq;
    Async::run<QVector<types::SessionRow>>(
        this,
        []() { return DbService::instance().sessionList(500, 0, QString(), QString()); },
        [this, seq](const QVector<types::SessionRow>& rows) {
            if (seq != m_listSeq)
                return; // a newer reload is already in flight
            m_listData = rows;
            m_listLoaded = true;
            renderList();
        });
}

void SessionsPage::renderList()
{
    const QString q = m_search->text().trimmed().toLower();
    const QString tt = m_taskType->currentData().toString();
    const QString sort = m_sort->currentData().toString();

    QVector<types::SessionRow> items;
    for (const types::SessionRow& s : m_listData) {
        if (!tt.isEmpty() && s.taskType != tt)
            continue;
        if (!q.isEmpty()
            && !(s.id.toLower().contains(q) || s.title.toLower().contains(q)
                 || s.directory.toLower().contains(q)))
            continue;
        items.push_back(s);
    }
    std::sort(items.begin(), items.end(),
              [&sort](const types::SessionRow& a, const types::SessionRow& b) {
                  if (sort == QLatin1String("tokens"))
                      return a.totalTokens > b.totalTokens;
                  if (sort == QLatin1String("calls"))
                      return (a.modelCalls + a.toolCalls) > (b.modelCalls + b.toolCalls);
                  return a.timeUpdated > b.timeUpdated; // recent
              });

    m_list->blockSignals(true);
    m_list->clear();
    if (items.isEmpty()) {
        auto* item = new QListWidgetItem(m_list);
        item->setFlags(Qt::NoItemFlags); // placeholder row: not clickable
        auto* empty = new QLabel(QStringLiteral("无匹配会话"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setProperty("cls", "faint");
        empty->setStyleSheet("padding:24px;");
        item->setSizeHint(QSize(100, 60));
        m_list->setItemWidget(item, empty);
    }
    int restoreRow = -1;
    for (const types::SessionRow& s : items) {
        auto* item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, s.id);
        auto* w = new SessionItemWidget(s);
        item->setSizeHint(w->sizeHint());
        m_list->setItemWidget(item, w);
        if (s.id == m_currentId)
            restoreRow = m_list->count() - 1;
    }
    if (restoreRow >= 0)
        m_list->setCurrentRow(restoreRow);
    m_list->blockSignals(false);
}

void SessionsPage::onListItemClicked(int row)
{
    if (row < 0)
        return;
    const auto* item = m_list->item(row);
    if (!item)
        return;
    m_currentId = item->data(Qt::UserRole).toString();
    m_currentTab = QStringLiteral("timeline");
    m_detailStack->setCurrentIndex(0); // show the tabs
    m_tabs->blockSignals(true);
    m_tabs->setCurrentIndex(0);
    m_tabs->blockSignals(false);
    loadDetailHead();
    loadTab();
}

void SessionsPage::openSession(const QString& sessionId, const QString& tab)
{
    m_currentId = sessionId;
    m_currentTab = tab.isEmpty() ? QStringLiteral("timeline") : tab;
    m_detailStack->setCurrentIndex(0); // show the tabs

    // select the row in the list if present
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == sessionId) {
            m_list->blockSignals(true);
            m_list->setCurrentRow(i);
            m_list->blockSignals(false);
            break;
        }
    }
    static const QStringList order = {"timeline", "context", "turns", "agents", "tasks", "usage", "state"};
    int idx = order.indexOf(m_currentTab);
    if (idx < 0) {
        idx = 0;
        m_currentTab = QStringLiteral("timeline"); // keep state in sync with the UI
    }
    m_tabs->blockSignals(true);
    m_tabs->setCurrentIndex(idx);
    m_tabs->blockSignals(false);

    loadDetailHead();
    loadTab();
}

void SessionsPage::onTabChanged(int)
{
    // update tab id, then lazy-load content
    static const QStringList order = {"timeline", "context", "turns", "agents", "tasks", "usage", "state"};
    if (m_tabs->currentIndex() >= 0 && m_tabs->currentIndex() < order.size())
        m_currentTab = order.at(m_tabs->currentIndex());
    loadTab();
}

void SessionsPage::loadDetailHead()
{
    if (m_currentId.isEmpty())
        return;
    const QString id = m_currentId;
    const int seq = ++m_headSeq;
    Async::run<types::SessionDetail>(
        this,
        [id]() { return DbService::instance().sessionGet(id); },
        [this, seq, id](const types::SessionDetail& s) {
            if (seq != m_headSeq || id != m_currentId || !s.found)
                return;
            m_title->setText(s.title.isEmpty() ? QStringLiteral("(无标题)") : s.title);
            QStringList sub;
            sub << s.id;
            if (!s.parentId.isEmpty())
                sub << QStringLiteral("parent ") + s.parentId.left(12);
            if (!s.directory.isEmpty())
                sub << s.directory;
            m_titleSub->setText(sub.join(QStringLiteral(" · ")));
        });
}

void SessionsPage::loadTab()
{
    if (m_currentId.isEmpty())
        return;
    const QString id = m_currentId;
    if (m_currentTab == QLatin1String("timeline")) {
        m_timelineTab->load(id); // async internally
    } else if (m_currentTab == QLatin1String("context")) {
        m_contextTab->load(id); // async internally
    } else if (m_currentTab == QLatin1String("turns")
               || m_currentTab == QLatin1String("usage")) {
        const int seq = ++m_tabSeq;
        Async::run<QVector<types::TurnRow>>(
            this,
            [id]() { return DbService::instance().sessionTurns(id); },
            [this, seq, id](const QVector<types::TurnRow>& turns) {
                if (seq != m_tabSeq || id != m_currentId)
                    return;
                if (m_currentTab == QLatin1String("turns"))
                    renderTurns(turns);
                else
                    renderUsage(turns);
            });
    } else if (m_currentTab == QLatin1String("agents")) {
        const int seq = ++m_tabSeq;
        Async::run<QVector<types::ChildAgent>>(
            this,
            [id]() { return DbService::instance().sessionChildrenEnriched(id); },
            [this, seq, id](const QVector<types::ChildAgent>& children) {
                if (seq != m_tabSeq || id != m_currentId)
                    return;
                renderAgents(children);
            });
    } else if (m_currentTab == QLatin1String("tasks")) {
        const int seq = ++m_tabSeq;
        Async::run<QVector<types::TodoRow>>(
            this,
            [id]() { return DbService::instance().todosForSession(id); },
            [this, seq, id](const QVector<types::TodoRow>& todos) {
                if (seq != m_tabSeq || id != m_currentId)
                    return;
                renderTasks(todos);
            });
    } else if (m_currentTab == QLatin1String("state")) {
        const int seq = ++m_tabSeq;
        Async::run<types::SessionDetail>(
            this,
            [id]() { return DbService::instance().sessionGet(id); },
            [this, seq, id](const types::SessionDetail& detail) {
                if (seq != m_tabSeq || id != m_currentId)
                    return;
                renderState(detail);
            });
    }
}

// ── Turns tab ─────────────────────────────────────────────────

QWidget* SessionsPage::buildTurnsTab()
{
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(16, 14, 16, 16);
    lay->setSpacing(0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("turnsScroll"));
    auto* host = new QWidget;
    auto* hl = new QVBoxLayout(host);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(5);
    auto* countLabel = new QLabel;
    countLabel->setObjectName(QStringLiteral("turnsCount"));
    countLabel->setProperty("cls", "faint");
    hl->addWidget(countLabel);
    hl->addStretch(1);
    scroll->setWidget(host);
    lay->addWidget(scroll, 1);
    return w;
}

void SessionsPage::renderTurns(const QVector<types::TurnRow>& turns)
{
    auto* scroll = m_turnsTab->findChild<QScrollArea*>(QStringLiteral("turnsScroll"));
    if (!scroll)
        return;
    auto* hl = qobject_cast<QVBoxLayout*>(
        scroll->widget() ? scroll->widget()->layout() : nullptr);
    if (!hl)
        return;

    // web header: "Turn 时间线 · N 个 turn"
    if (QLabel* head = m_turnsTab->findChild<QLabel*>(QStringLiteral("turnsCount"))) {
        head->setText(turns.isEmpty() ? QString()
                                      : QStringLiteral("%1 个 turn").arg(turns.size()));
    }

    // clear previous rows (keep the trailing stretch)
    while (hl->count() > 1) {
        auto* item = hl->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    if (turns.isEmpty()) {
        hl->insertWidget(0, UiUtil::label(QStringLiteral("无 turn 记录"), "faint"));
        return;
    }

    qint64 maxDur = 1;
    for (const types::TurnRow& t : turns)
        maxDur = qMax(maxDur, t.durationMs);

    const Palette& pal = Theme::instance().pal();
    int idx = 0;
    for (const types::TurnRow& t : turns) {
        auto* row = new QWidget;
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(11, 7, 11, 7);
        rl->setSpacing(11);
        auto* dur = new QLabel(t.durationOk ? Format::fmtDur(double(t.durationMs) / 1000.0)
                                             : QStringLiteral("…"));
        dur->setStyleSheet(
            QStringLiteral("color:%1;font-family:'Consolas';font-size:8.5pt;")
                .arg(pal.fg3.name()));
        dur->setFixedWidth(80);
        rl->addWidget(dur);

        auto* mid = new QWidget;
        auto* ml = new QVBoxLayout(mid);
        ml->setContentsMargins(0, 0, 0, 0);
        ml->setSpacing(3);
        const QColor fill = t.status == QLatin1String("error")
                ? pal.sevErr
                : (t.status == QLatin1String("cancelled") ? pal.sevWarn : pal.accent);
        const int pct = int(qMin<qint64>(100, t.durationMs * 100 / maxDur));
        ml->addWidget(new TurnBar(pct, fill));
        QStringList sub;
        sub << t.turnId.left(16);
        if (t.contextExceeded)
            sub << QStringLiteral("· ⚠ context_exceeded");
        if (!t.errorType.isEmpty())
            sub << QStringLiteral("· ") + t.errorType;
        auto* subL = new QLabel(sub.join(QStringLiteral(" ")));
        subL->setStyleSheet(
            QStringLiteral("color:%1;font-family:'Consolas';font-size:7.5pt;")
                .arg(pal.fg4.name()));
        ml->addWidget(subL);
        rl->addWidget(mid, 1);

        auto* stats = new QLabel(QStringLiteral("%1 req · %2 tools · %3 tok")
                                      .arg(Format::fmtInt(double(t.modelRequestCount)),
                                           Format::fmtInt(double(t.toolCallCount)),
                                           Format::fmtNum(double(t.computedTotalTokens))));
        stats->setStyleSheet(
            QStringLiteral("color:%1;font-family:'Consolas';font-size:8pt;")
                .arg(pal.fg4.name()));
        stats->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rl->addWidget(stats);
        hl->insertWidget(idx++, row);
    }
}

// ── Agents tab ───────────────────────────────────────────────

QWidget* SessionsPage::buildAgentsTab()
{
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(16, 14, 16, 16);
    lay->setSpacing(8);
    auto* empty = UiUtil::label(QStringLiteral("该会话未派生子 agent"), "faint");
    empty->setObjectName(QStringLiteral("agentsEmpty"));
    empty->setAlignment(Qt::AlignCenter);
    empty->setStyleSheet(QStringLiteral("padding:28px;background:transparent;"));
    auto* table = new RowHoverTable;
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({QStringLiteral("profile"), QStringLiteral("描述"),
                                       QStringLiteral("token"), QStringLiteral("创建"),
                                       QString()});
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(table, &QTableWidget::cellDoubleClicked, this,
            [this, table](int row, int) {
                const QTableWidgetItem* it = table->item(row, 0);
                if (it) {
                    const QString id = it->data(Qt::UserRole).toString();
                    if (!id.isEmpty())
                        emit openSessionRequested(id, QStringLiteral("timeline"));
                }
            });
    lay->addWidget(empty);
    lay->addWidget(table);
    return w;
}

void SessionsPage::renderAgents(const QVector<types::ChildAgent>& children)
{
    auto* table = m_agentsTab->findChild<QTableWidget*>();
    auto* empty = m_agentsTab->findChild<QLabel*>(QStringLiteral("agentsEmpty"));
    if (!table)
        return;
    if (children.isEmpty()) {
        table->setRowCount(0);
        if (empty)
            empty->show();
        return;
    }
    if (empty)
        empty->hide();
    const Palette& pal = Theme::instance().pal();
    table->setRowCount(int(children.size()));
    for (int i = 0; i < children.size(); ++i) {
        const types::ChildAgent& c = children[i];
        auto* profile = cellTxt(c.profile.isEmpty() ? QStringLiteral("?") : c.profile);
        profile->setForeground(Theme::instance().badgeColor(
            c.profile == QLatin1String("Explore") ? QStringLiteral("teal")
                                                  : QStringLiteral("purple")));
        profile->setData(Qt::UserRole, c.id); // deep link target
        profile->setToolTip(c.prompt);
        table->setItem(i, 0, profile);
        QString prompt = c.prompt;
        if (prompt.size() > 80)
            prompt = prompt.left(80) + QStringLiteral("…");
        table->setItem(i, 1, cellTxt(prompt));
        table->setItem(i, 2,
                       cellNum(c.totalTokensOk ? Format::fmtNum(double(c.totalTokens))
                                                : QStringLiteral("0")));
        auto* created = cellTxt(Format::relTime(c.timeCreatedMs));
        created->setForeground(pal.fg4);
        table->setItem(i, 3, created);
        auto* open = cellTxt(QStringLiteral("打开 →"));
        open->setForeground(pal.accent);
        open->setData(Qt::UserRole, c.id);
        table->setItem(i, 4, open);
    }
    table->resizeColumnsToContents();
}

// ── Tasks tab ────────────────────────────────────────────────

QWidget* SessionsPage::buildTasksTab()
{
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(16, 14, 16, 16);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget;
    auto* hl = new QVBoxLayout(host);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(3);
    hl->addStretch(1);
    scroll->setWidget(host);
    lay->addWidget(scroll, 1);
    return w;
}

void SessionsPage::renderTasks(const QVector<types::TodoRow>& todos)
{
    auto* scroll = m_tasksTab->findChildren<QScrollArea*>().value(0);
    if (!scroll)
        return;
    auto* hl = qobject_cast<QVBoxLayout*>(
        scroll->widget() ? scroll->widget()->layout() : nullptr);
    if (!hl)
        return;
    while (hl->count() > 1) {
        auto* item = hl->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    if (todos.isEmpty()) {
        hl->insertWidget(0, UiUtil::label(QStringLiteral("无 todo 记录"), "faint"));
        return;
    }
    const Palette& pal = Theme::instance().pal();
    int idx = 0;
    for (const types::TodoRow& t : todos) {
        auto* row = new QWidget;
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 5, 0, 5);
        rl->setSpacing(10);
        rl->addWidget(new BadgeLabel(t.status, todoStatusKind(t.status)));
        rl->addWidget(new BadgeLabel(t.priority, todoPriorityKind(t.priority)));
        auto* content = new QLabel(t.content);
        content->setWordWrap(true);
        content->setStyleSheet(
            QStringLiteral("color:%1;font-size:9pt;").arg(pal.fg1.name()));
        rl->addWidget(content, 1);
        hl->insertWidget(idx++, row);
    }
}

// ── Usage tab ────────────────────────────────────────────────

QWidget* SessionsPage::buildUsageTab()
{
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(16, 14, 16, 16);
    auto* table = new RowHoverTable;
    table->setColumnCount(11);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("turn"), QStringLiteral("状态"), QStringLiteral("req"),
         QStringLiteral("tools"), QStringLiteral("tool err"), QStringLiteral("input"),
         QStringLiteral("output"), QStringLiteral("reasoning"),
         QStringLiteral("cache read"), QStringLiteral("total"), QStringLiteral("耗时")});
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    lay->addWidget(table);
    return w;
}

void SessionsPage::renderUsage(const QVector<types::TurnRow>& turns)
{
    auto* table = m_usageTab->findChild<QTableWidget*>();
    if (!table)
        return;
    if (turns.isEmpty()) {
        table->setRowCount(0);
        return;
    }
    const Palette& pal = Theme::instance().pal();
    table->setRowCount(int(turns.size()));
    for (int i = 0; i < turns.size(); ++i) {
        const types::TurnRow& t = turns[i];
        auto* turnId = cellTxt(t.turnId.left(10));
        turnId->setForeground(pal.fg4);
        turnId->setFont(QFont(QStringLiteral("Consolas"), 8));
        table->setItem(i, 0, turnId);
        table->setCellWidget(i, 1,
                              new BadgeLabel(t.status, statusBadgeKind(t.status)));
        table->setItem(i, 2, cellNum(Format::fmtInt(double(t.modelRequestCount))));
        table->setItem(i, 3, cellNum(Format::fmtInt(double(t.toolCallCount))));
        auto* terr = cellNum(QString::number(t.toolErrorCount));
        terr->setForeground(t.toolErrorCount > 0 ? pal.sevErr : pal.fg1);
        table->setItem(i, 4, terr);
        table->setItem(i, 5, cellNum(Format::fmtInt(double(t.inputTokens))));
        table->setItem(i, 6, cellNum(Format::fmtInt(double(t.outputTokens))));
        auto* reason = cellNum(Format::fmtInt(double(t.reasoningTokens)));
        if (t.reasoningTokens > 0)
            reason->setForeground(pal.accent2);
        table->setItem(i, 7, reason);
        table->setItem(i, 8, cellNum(Format::fmtInt(double(t.cacheReadTokens))));
        table->setItem(i, 9, cellNum(Format::fmtInt(double(t.computedTotalTokens))));
        table->setItem(i, 10,
                       cellNum(t.durationOk ? Format::fmtMs(double(t.durationMs))
                                            : QStringLiteral("—")));
    }
    table->resizeColumnsToContents();
}

// ── State tab ─────────────────────────────────────────────────

QWidget* SessionsPage::buildStateTab()
{
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(16, 14, 16, 16);
    auto* card = new CardFrame;
    auto* grid = new QGridLayout(card);
    grid->setContentsMargins(14, 14, 14, 14);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 1);
    lay->addWidget(card);
    lay->addStretch(1);
    return w;
}

void SessionsPage::renderState(const types::SessionDetail& detail)
{
    auto* grid = m_stateTab->findChild<QGridLayout*>();
    if (!grid)
        return;
    while (grid->count()) {
        auto* item = grid->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    if (!detail.found)
        return;

    const Palette& pal = Theme::instance().pal();
    const auto valueText = [](const QVariant& v) -> QString {
        if (v.isNull() || !v.isValid())
            return QStringLiteral("null");
        return v.toString();
    };

    const QList<QPair<QString, QVariant>> rows = {
        {QStringLiteral("id"), detail.allColumns.value(QStringLiteral("id"))},
        {QStringLiteral("title"), detail.allColumns.value(QStringLiteral("title"))},
        {QStringLiteral("task_type"), detail.allColumns.value(QStringLiteral("task_type"))},
        {QStringLiteral("parent_id"), detail.allColumns.value(QStringLiteral("parent_id"))},
        {QStringLiteral("workspace_id"),
         detail.allColumns.value(QStringLiteral("workspace_id"))},
        {QStringLiteral("project_id"), detail.allColumns.value(QStringLiteral("project_id"))},
        {QStringLiteral("directory"), detail.allColumns.value(QStringLiteral("directory"))},
        {QStringLiteral("permission"),
         detail.allColumns.value(QStringLiteral("permission"))},
        {QStringLiteral("trace_id"), detail.allColumns.value(QStringLiteral("trace_id"))},
        {QStringLiteral("time_created"), QVariant(Format::fmtTimeFull(detail.timeCreated))},
        {QStringLiteral("time_updated"), QVariant(Format::fmtTimeFull(detail.timeUpdated))},
        {QStringLiteral("summary_files"),
         detail.allColumns.value(QStringLiteral("summary_files"))},
        {QStringLiteral("summary_additions"),
         detail.allColumns.value(QStringLiteral("summary_additions"))},
        {QStringLiteral("summary_deletions"),
         detail.allColumns.value(QStringLiteral("summary_deletions"))},
        {QStringLiteral("share_url"), detail.allColumns.value(QStringLiteral("share_url"))},
    };
    int r = 0;
    for (const auto& [key, val] : rows) {
        auto* k = new QLabel(key);
        k->setStyleSheet(QStringLiteral("color:%1;font-size:9pt;").arg(pal.fg4.name()));
        auto* v = new QLabel(valueText(val));
        v->setStyleSheet(
            QStringLiteral("color:%1;font-family:'Consolas';font-size:8.5pt;")
                .arg(pal.fg1.name()));
        v->setWordWrap(true);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(k, r, 0);
        grid->addWidget(v, r, 1);
        ++r;
    }
}
