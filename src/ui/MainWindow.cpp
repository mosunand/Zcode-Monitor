// MainWindow.cpp — see MainWindow.h.

#include "MainWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "core/DbService.h"
#include "core/Paths.h"
#include "core/RuntimeWatchdog.h"
#include "ui/pages/AgentsPage.h"
#include "ui/pages/ErrorsPage.h"
#include "ui/pages/HowPage.h"
#include "ui/pages/OverviewPage.h"
#include "ui/pages/RawPage.h"
#include "ui/pages/SessionsPage.h"
#include "Theme.h"
#include "UiUtil.h"
#include "util/Async.h"
#include "util/Frost.h"
#include "util/Format.h"

using namespace UiUtil;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("zcode-monitor"));
    // 毛玻璃模式需要窗口 backing store 支持半透明（frost 关闭时 QSS 全不透明，无影响）
    setAttribute(Qt::WA_TranslucentBackground);
    resize(1500, 920);

    m_watchdog = new RuntimeWatchdog(this);
    // records emitted from pool threads (async checkpoint) land here on the
    // GUI thread — the only place m_lastCheckpoint is ever written now
    connect(m_watchdog, &RuntimeWatchdog::checkpointDone, this,
            [this](const types::CheckpointRecord& rec) {
                m_watchdog->recordCheckpoint(rec);
            });

    auto* central = new QWidget(this);
    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(buildTopbar());

    m_stack = new QStackedWidget;
    m_overviewPage = new OverviewPage;
    m_sessionsPage = new SessionsPage;
    m_agentsPage = new AgentsPage;
    m_errorsPage = new ErrorsPage;
    m_rawPage = new RawPage;
    m_howPage = new HowPage;
    m_stack->addWidget(m_overviewPage);
    m_stack->addWidget(m_sessionsPage);
    m_stack->addWidget(m_agentsPage);
    m_stack->addWidget(m_errorsPage);
    m_stack->addWidget(m_rawPage);
    m_stack->addWidget(m_howPage);
    outer->addWidget(m_stack, 1);
    setCentralWidget(central);

    // toast overlay (sits on top of the central widget)
    m_toast = new QLabel(central);
    m_toast->setObjectName(QStringLiteral("toast"));
    m_toast->hide();
    m_toastTimer.setSingleShot(true);
    m_toastTimer.setInterval(2400);
    connect(&m_toastTimer, &QTimer::timeout, m_toast, &QLabel::hide);

    // deep links from other pages
    connect(m_sessionsPage, &SessionsPage::openSessionRequested,
            this, [this](const QString& id, const QString& tab) { openSession(id, tab); });
    connect(m_agentsPage, &AgentsPage::openSessionRequested,
            this, [this](const QString& id, const QString& tab) { openSession(id, tab); });
    connect(m_errorsPage, &ErrorsPage::openSessionRequested,
            this, [this](const QString& id, const QString& tab) { openSession(id, tab); });
    connect(m_howPage, &HowPage::openSessionRequested,
            this, [this](const QString& id, const QString& tab) { openSession(id, tab); });
    connect(m_howPage, &HowPage::navigateRequested, this, &MainWindow::navigateTo);

    // theme button reacts to 't' shortcut too; the status dot color is taken
    // from the palette at healthTick time, so refresh it immediately on flips.
    // frost flips re-apply the DWM backdrop on this window.
    connect(&Theme::instance(), &Theme::changed, this, [this](bool) {
        updateThemeButton();
        applyFrost();
        const Palette& pal = Theme::instance().pal();
        m_statusDot->setColor(m_lastDbOk ? pal.sevOk : pal.sevErr);
    });

    // keyboard: 't' toggles theme (unless typing in an input)
    auto* sc = new QShortcut(QKeySequence(Qt::Key_T), this);
    connect(sc, &QShortcut::activated, this, [this]() {
        QWidget* focus = QApplication::focusWidget();
        if (qobject_cast<QLineEdit*>(focus) || qobject_cast<QComboBox*>(focus)
            || qobject_cast<QTextEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus))
            return;
        Theme::instance().toggle();
    });

    // health loop (app.js healthLoop: every 5s)
    connect(&m_healthTimer, &QTimer::timeout, this, &MainWindow::healthTick);
    m_healthTimer.start(5000);
    healthTick();

    navigateTo(QStringLiteral("overview"));
}

QWidget* MainWindow::buildTopbar()
{
    auto* bar = new QFrame;
    bar->setObjectName(QStringLiteral("topbar"));
    bar->setFixedHeight(44);
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(14, 0, 14, 0);
    lay->setSpacing(12);

    m_statusDot = new Dot(8);
    m_statusDot->setColor(Theme::instance().pal().sevOk);

    auto* brand = new QLabel(QStringLiteral("zcode-monitor"));
    brand->setObjectName(QStringLiteral("brand"));

    // nav (web topbar order)
    struct Nav {
        const char* id;
        const char* label;
    };
    const QVector<Nav> navs = {
        {"overview", "实时监控"}, {"sessions", "会话"}, {"agents", "子 Agent"},
        {"errors", "错误与链路"}, {"raw", "原始数据"}, {"how", "运行原理"},
    };
    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    for (const Nav& n : navs) {
        auto* btn = new QToolButton;
        btn->setText(QString::fromUtf8(n.label));
        btn->setProperty("cls", "nav");
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        const QString page = QString::fromLatin1(n.id);
        connect(btn, &QToolButton::clicked, this, [this, page] { navigateTo(page); });
        m_navGroup->addButton(btn);
        lay->addWidget(btn);
    }

    lay->addSpacing(6);
    lay->addStretch(1);

    m_meta = new QLabel(QStringLiteral("—"));
    m_meta->setObjectName(QStringLiteral("metaLabel"));
    lay->addWidget(m_meta);

    m_checkpointBtn = new QToolButton;
    m_checkpointBtn->setText(QStringLiteral("💾"));
    m_checkpointBtn->setProperty("cls", "ghost");
    m_checkpointBtn->setToolTip(QStringLiteral("把 WAL 数据合并进主库，确保 ZCode 关闭后历史可读"));
    m_checkpointBtn->setCursor(Qt::PointingHandCursor);
    connect(m_checkpointBtn, &QToolButton::clicked, this, &MainWindow::onCheckpointClicked);
    lay->addWidget(m_checkpointBtn);

    m_themeBtn = new QToolButton;
    m_themeBtn->setProperty("cls", "ghost");
    m_themeBtn->setCursor(Qt::PointingHandCursor);
    m_themeBtn->setToolTip(QStringLiteral("切换主题 (t)"));
    connect(m_themeBtn, &QToolButton::clicked, this, &MainWindow::onThemeClicked);
    lay->addWidget(m_themeBtn);
    updateThemeButton();

    auto* frostBtn = new QToolButton;
    frostBtn->setText(QStringLiteral("❄"));
    frostBtn->setProperty("cls", "ghost");
    frostBtn->setCursor(Qt::PointingHandCursor);
    frostBtn->setToolTip(QStringLiteral("毛玻璃背景 开/关"));
    frostBtn->setCheckable(true);
    frostBtn->setChecked(Theme::instance().isFrost());
    connect(frostBtn, &QToolButton::clicked, this, [this, frostBtn](bool on) {
        Theme::instance().setFrost(on);
        frostBtn->setChecked(Theme::instance().isFrost());
    });
    lay->addWidget(frostBtn);

    lay->insertWidget(0, m_statusDot);
    lay->insertWidget(1, brand);
    return bar;
}

void MainWindow::updateThemeButton()
{
    if (m_themeBtn)
        m_themeBtn->setText(Theme::instance().isDark() ? QStringLiteral("🌙")
                                                        : QStringLiteral("☀️"));
}

void MainWindow::navigateTo(const QString& page)
{
    // map page id → index (order of addWidget)
    static const QStringList order = {"overview", "sessions", "agents", "errors", "raw", "how"};
    int idx = order.indexOf(page);
    if (idx < 0)
        idx = 0;
    m_stack->setCurrentIndex(idx);
    const auto buttons = m_navGroup->buttons();
    if (idx < buttons.size()) {
        m_navGroup->blockSignals(true);
        buttons[idx]->setChecked(true);
        m_navGroup->blockSignals(false);
    }
}

void MainWindow::openSession(const QString& sessionId, const QString& tab)
{
    navigateTo(QStringLiteral("sessions"));
    m_sessionsPage->openSession(sessionId, tab);
}

void MainWindow::healthTick()
{
    // probe off-thread: a contended read can busy-retry for ~1.6s and that
    // sleep must never happen on the GUI thread (window freeze)
    Async::run<bool>(
        this,
        []() { return DbService::instance().probe(); },
        [this](const bool ok) {
            m_lastDbOk = ok; // theme flips re-apply the dot color without re-probing
            renderHealthMeta(ok);
        });
}

void MainWindow::renderHealthMeta(bool ok)
{
    QStringList parts;
    const Palette& pal = Theme::instance().pal();
    if (ok) {
        m_statusDot->setColor(pal.sevOk);
        parts << QStringLiteral("DB OK");
    } else {
        m_statusDot->setColor(pal.sevErr);
        parts << (DbService::instance().lastError().isEmpty()
                      ? QStringLiteral("DB 不可读")
                      : QStringLiteral("DB 重试中"));
    }
    const bool zcodeRunning = m_watchdog->isZcodeRunning();
    parts << (zcodeRunning ? QStringLiteral("ZCode 运行中") : QStringLiteral("ZCode 已退出"));

    const types::WalStatus wal = RuntimeWatchdog::walStatus(Paths::dbPath());
    if (wal.valid && wal.walBytes > 0)
        parts << QStringLiteral("WAL %1MB 待合并")
                     .arg(QString::number(double(wal.walBytes) / 1024.0 / 1024.0, 'f', 1));
    // richer status line: DB/ZCode state and pending WAL get tinted spans
    QString meta;
    for (int i = 0; i < parts.size(); ++i) {
        if (i > 0)
            meta += QStringLiteral("<span style='color:%1'> · </span>")
                        .arg(pal.fg5.name());
        const QColor c = parts.at(i).startsWith(QLatin1String("DB"))
                             ? (ok ? pal.sevOk : pal.sevErr)
                             : pal.fg3;
        meta += QStringLiteral("<span style='color:%1'>%2</span>").arg(c.name(), parts.at(i));
    }
    m_meta->setTextFormat(Qt::RichText);
    m_meta->setText(meta);
}

void MainWindow::onCheckpointClicked()
{
    // checkpoint holds a writable connection for up to 10s (busy_timeout) —
    // run it off the GUI thread so the window never freezes, and disable the
    // button + ⏳ while it works (web behavior).
    if (m_checkpointBusy)
        return;
    m_checkpointBusy = true;
    m_checkpointBtn->setText(QStringLiteral("⏳"));
    m_checkpointBtn->setEnabled(false);
    Async::run<types::CheckpointResult>(
        this,
        [this]() { return m_watchdog->requestCheckpoint(true); },
        [this](const types::CheckpointResult& r) {
            m_checkpointBusy = false;
            m_checkpointBtn->setText(QStringLiteral("💾"));
            m_checkpointBtn->setEnabled(true);
            if (r.ok) {
                if (r.before.valid && r.after.valid) {
                    const qint64 folded = r.before.walBytes - r.after.walBytes;
                    showToast(QStringLiteral("已合并 WAL：%1MB 数据并入主库")
                                  .arg(QString::number(double(folded) / 1024.0 / 1024.0, 'f', 1)));
                } else {
                    showToast(QStringLiteral("checkpoint 完成"));
                }
                healthTick();
            } else if (r.error == QLatin1String("zcode_running")) {
                showToast(QStringLiteral("ZCode 运行中，请先关闭 ZCode 再点（或点此强制）"));
            } else {
                showToast(QStringLiteral("checkpoint 失败：") + r.error);
            }
        });
}

void MainWindow::onThemeClicked()
{
    Theme::instance().toggle();
}

void MainWindow::showToast(const QString& msg)
{
    m_toast->setText(msg);
    m_toast->adjustSize();
    const int x = centralWidget()->width() - m_toast->width() - 18;
    const int y = centralWidget()->height() - m_toast->height() - 18;
    m_toast->move(qMax(0, x), qMax(0, y));
    m_toast->raise();
    m_toast->show();
    m_toastTimer.start();
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    if (m_toast && m_toast->isVisible()) {
        const int x = centralWidget()->width() - m_toast->width() - 18;
        const int y = centralWidget()->height() - m_toast->height() - 18;
        m_toast->move(qMax(0, x), qMax(0, y));
    }
}

void MainWindow::applyFrost()
{
    Frost::apply(this, Theme::instance().isFrost(), Theme::instance().isDark());
}

// ── GUI assembly ─────────────────────────────────────────────

int runGui(QApplication& app, const QString& screenshotDir, const QString& clickDir,
           bool clickReal)
{
    QApplication::setApplicationName(QStringLiteral("zcode-monitor"));
    QApplication::setOrganizationName(QStringLiteral("zhipucode"));
    QFont base(QStringLiteral("Segoe UI"), 10);
    QApplication::setFont(base);
    Theme::instance().init();
    // window/taskbar icon comes from the embedded multi-resolution ico
    app.setWindowIcon(QIcon(QStringLiteral(":/zcode-monitor.ico")));

    MainWindow w;
    w.show();
    w.applyFrost(); // DWM 毛玻璃（需在窗口原生句柄创建后应用）
    if (!clickDir.isNull())
        w.clickTour(clickDir, clickReal);
    else if (!screenshotDir.isNull())
        w.screenshotTour(screenshotDir);
    return app.exec();
}

// ── screenshot tour (automated visual verification) ──────────

void MainWindow::screenshotTour(const QString& outDir)
{
    // deterministic opaque screenshots: frost off for the tour
    Theme::instance().setFrost(false);
    // pick one interactive session (context/turns/usage make sense) and one
    // subagent session (timeline makes sense)
    QString interactiveId, subagentId;
    const auto sessions = DbService::instance().sessionList(500);
    for (const types::SessionRow& s : sessions) {
        if (interactiveId.isEmpty() && s.taskType == QLatin1String("interactive"))
            interactiveId = s.id;
        if (subagentId.isEmpty() && s.taskType == QLatin1String("subagent_child"))
            subagentId = s.id;
        if (!interactiveId.isEmpty() && !subagentId.isEmpty())
            break;
    }
    const QString detailId = interactiveId.isEmpty() ? subagentId : interactiveId;

    m_tourDir = outDir;
    m_tourSteps.clear();
    // deterministic screenshots: always start from the dark theme
    Theme::instance().setTheme(true);
    m_tourSteps.push_back({QStringLiteral("01-overview"), 0, QStringLiteral("overview"), QString(), QString()});
    m_tourSteps.push_back({QStringLiteral("02-sessions-list"), 0, QStringLiteral("sessions"), QString(), QString()});
    if (!subagentId.isEmpty())
        m_tourSteps.push_back({QStringLiteral("03-session-timeline"), 1, QString(), subagentId,
                               QStringLiteral("timeline")});
    if (!detailId.isEmpty())
        m_tourSteps.push_back({QStringLiteral("04-session-context"), 1, QString(), detailId,
                               QStringLiteral("context")});
    if (!detailId.isEmpty())
        m_tourSteps.push_back({QStringLiteral("05-session-turns"), 1, QString(), detailId,
                               QStringLiteral("turns")});
    if (!detailId.isEmpty())
        m_tourSteps.push_back({QStringLiteral("06-session-agents"), 1, QString(), detailId,
                               QStringLiteral("agents")});
    if (!detailId.isEmpty())
        m_tourSteps.push_back({QStringLiteral("07-session-tasks"), 1, QString(), detailId,
                               QStringLiteral("tasks")});
    if (!detailId.isEmpty())
        m_tourSteps.push_back({QStringLiteral("08-session-usage"), 1, QString(), detailId,
                               QStringLiteral("usage")});
    if (!detailId.isEmpty())
        m_tourSteps.push_back({QStringLiteral("09-session-state"), 1, QString(), detailId,
                               QStringLiteral("state")});
    m_tourSteps.push_back({QStringLiteral("10-agents-tree"), 0, QStringLiteral("agents"), QString(), QString()});
    m_tourSteps.push_back({QStringLiteral("11-errors"), 0, QStringLiteral("errors"), QString(), QString()});
    m_tourSteps.push_back({QStringLiteral("12-raw"), 0, QStringLiteral("raw"), QString(), QString()});
    m_tourSteps.push_back({QStringLiteral("13-how"), 0, QStringLiteral("how"), QString(), QString()});
    m_tourSteps.push_back({QStringLiteral("14-overview-light"), 3, QStringLiteral("overview"), QString(), QString()});
    // round-trip: flip back to dark and make sure the UI (and the status
    // dot) re-applies dark colors — regression guard for the theme-timing bug
    m_tourSteps.push_back({QStringLiteral("15-overview-dark-back"), 4, QStringLiteral("overview"), QString(), QString()});
    m_tourIndex = 0;

    resize(1500, 920);
    QTimer::singleShot(1200, this, &MainWindow::tourAdvance);
}

void MainWindow::tourAdvance()
{
    if (m_tourIndex >= m_tourSteps.size()) {
        Theme::instance().setFrost(true); // 恢复用户的毛玻璃设置
        QApplication::quit();
        return;
    }
    const TourStep s = m_tourSteps.at(m_tourIndex);
    if (s.action == 2)
        Theme::instance().toggle();
    else if (s.action == 3)
        Theme::instance().setTheme(false); // explicit light theme
    else if (s.action == 4)
        Theme::instance().setTheme(true); // explicit dark theme
    if (s.action == 0 || s.action == 3 || s.action == 4)
        navigateTo(s.page);
    else if (s.action == 1)
        openSession(s.sessionId, s.tab);

    QTimer::singleShot(900, this, [this, s] {
        const QPixmap pm = grab();
        pm.save(m_tourDir + QLatin1Char('/') + s.name + QStringLiteral(".png"));
        printf("[shot] %s\n", qPrintable(s.name));
        fflush(stdout);
        ++m_tourIndex;
        tourAdvance();
    });
}

// ── click tour (interaction fuzz — crash reproduction) ───────

namespace {

#ifdef Q_OS_WIN
// real-input mode: when true, synthClick/synthWheel drive the OS cursor via
// SendInput (same path as the hardware mouse) instead of sendEvent.
bool g_realInputMode = false;

// inject a REAL cursor event through SendInput: identical path to hardware
// input (OS → Qt native handler → QApplication::notify), unlike sendEvent
// which bypasses grabs/focus/native processing. Returns false when the OS
// rejects the injection.
bool sendRealMouse(DWORD flags, LONG dx, LONG dy, DWORD data = 0)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.mouseData = data;
    return ::SendInput(1, &in, sizeof(INPUT)) == 1;
}

bool realMoveTo(const QPoint& global)
{
    // absolute coordinates are normalized to 0..65535 per primary monitor
    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);
    const LONG nx = LONG(qRound(double(global.x()) * 65535.0 / qMax(1, sw)));
    const LONG ny = LONG(qRound(double(global.y()) * 65535.0 / qMax(1, sh)));
    return sendRealMouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, nx, ny);
}

bool realClickAt(const QPoint& global)
{
    if (!realMoveTo(global))
        return false;
    if (!sendRealMouse(MOUSEEVENTF_LEFTDOWN, 0, 0))
        return false;
    return sendRealMouse(MOUSEEVENTF_LEFTUP, 0, 0);
}

bool realWheelAt(const QPoint& global, int delta)
{
    if (!realMoveTo(global))
        return false;
    return sendRealMouse(MOUSEEVENTF_WHEEL, 0, 0, DWORD(delta));
}
#endif // Q_OS_WIN

void synthHover(QWidget* target, const QPoint& localPos)
{
    QMouseEvent hover(QEvent::HoverMove, localPos, target->mapToGlobal(localPos),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &hover);
    // and the matching plain move for widgets tracking mouse position
    QMouseEvent move(QEvent::MouseMove, localPos, target->mapToGlobal(localPos),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move);
}

void synthClick(QWidget* target, const QPoint& localPos)
{
    const QPoint global = target->mapToGlobal(localPos);
#ifdef Q_OS_WIN
    // real path first (SendInput); fall back to direct delivery when the OS
    // refuses injection (e.g. no interactive session)
    if (g_realInputMode && realClickAt(global)) {
        QApplication::processEvents(); // let the native event arrive
        return;
    }
#else
    Q_UNUSED(global);
#endif
    synthHover(target, localPos); // real users move before they press
    QMouseEvent press(QEvent::MouseButtonPress, localPos, target->mapToGlobal(localPos),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, localPos, target->mapToGlobal(localPos),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QApplication::sendEvent(target, &release);
}

void synthClickCenter(QWidget* w)
{
    if (w && w->isVisible())
        synthClick(w, w->rect().center());
}

void synthWheel(QWidget* target, int delta)
{
#ifdef Q_OS_WIN
    if (g_realInputMode && realWheelAt(target->mapToGlobal(target->rect().center()),
                                       delta)) {
        QApplication::processEvents();
        return;
    }
#endif
    const QPoint local = target->rect().center();
    QWheelEvent wheel(local, target->mapToGlobal(local),
                      QPoint(0, delta), QPoint(0, delta),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &wheel);
}

} // namespace

void MainWindow::clickTour(const QString& outDir, bool realInput)
{
#ifdef Q_OS_WIN
    g_realInputMode = realInput;
#endif
    m_clickDir = outDir;
    m_clickSteps.clear();

    // ── 1. every nav button ──
    const auto navs = m_navGroup->buttons();
    for (int i = 0; i < navs.size(); ++i) {
        const int idx = i;
        m_clickSteps.push_back({QStringLiteral("nav-%1").arg(i), [this, idx] {
            synthClickCenter(m_navGroup->buttons().at(idx));
        }});
    }

    // ── 2. sessions: click the first few list rows (real list clicks) ──
    m_clickSteps.push_back({QStringLiteral("goto-sessions"), [this] {
        navigateTo(QStringLiteral("sessions"));
    }});
    for (int r = 0; r < 5; ++r) {
        const int row = r;
        m_clickSteps.push_back({QStringLiteral("session-row-%1").arg(r), [this, row] {
            if (QListWidget* list = m_sessionsPage->findChild<QListWidget*>(
                    QStringLiteral("sessionList"))) {
                if (QListWidgetItem* it = list->item(row)) {
                    const QRect vr = list->visualItemRect(it);
                    synthClick(list->viewport(), vr.center());
                }
            }
        }});
    }

    // ── 3. every session tab (click the tab bar, not setCurrentIndex) ──
    for (int t = 0; t < 7; ++t) {
        const int idx = t;
        m_clickSteps.push_back({QStringLiteral("tab-%1").arg(idx), [this, idx] {
            if (QTabWidget* tabs = m_sessionsPage->findChild<QTabWidget*>()) {
                if (QTabBar* bar = tabs->tabBar()) {
                    if (idx < bar->count())
                        synthClick(bar, bar->tabRect(idx).center());
                }
            }
        }});
    }

    // ── 4. timeline filter buttons ──
    m_clickSteps.push_back({QStringLiteral("goto-timeline-tab"), [this] {
        openSession(firstTestSession(), QStringLiteral("timeline"));
    }});
    const char* filterNames[] = {"all", "prompt", "llm", "tool", "network", "usage", "errors"};
    for (int f = 0; f < 7; ++f) {
        const int idx = f;
        m_clickSteps.push_back({QStringLiteral("filter-%1").arg(filterNames[f]), [this, idx] {
            const auto btns = m_sessionsPage->findChildren<QPushButton*>();
            // timeline filter buttons live inside the timeline tab
            for (QPushButton* b : btns) {
                if (b->isVisible() && b->parentWidget()
                    && b->text() != QStringLiteral("还原")
                    && b->property("cls").toString() != QStringLiteral("ghost")
                    && idx >= 0) {
                    // click the idx-th visible filter button
                    static int counter = -1; // not reliable across steps; click all below
                    Q_UNUSED(counter);
                }
            }
            // simpler: click every visible filter button of the timeline tab
            // (identified as flat small buttons in a row)
            int clicked = 0;
            for (QPushButton* b : m_sessionsPage->findChildren<QPushButton*>()) {
                if (!b->isVisible())
                    continue;
                const QString t = b->text();
                if (t == QStringLiteral("全部") || t == QStringLiteral("prompt")
                    || t == QStringLiteral("llm→") || t == QStringLiteral("tools")
                    || t == QStringLiteral("network") || t == QStringLiteral("usage")
                    || t.startsWith(QLatin1String("⚠"))) {
                    if (clicked == idx) {
                        synthClickCenter(b);
                        return;
                    }
                    ++clicked;
                }
            }
        }});
    }

    // ── 5. context: collapsible parts, rail nodes, expand/collapse all ──
    m_clickSteps.push_back({QStringLiteral("goto-context-tab"), [this] {
        openSession(firstTestSession(), QStringLiteral("context"));
    }});
    m_clickSteps.push_back({"ctx-click-parts", [this] {
        // click the first few collapsible parts (reasoning/tool) — the exact
        // widgets a user expands
        const auto frames = m_sessionsPage->findChildren<QFrame*>();
        int clicked = 0;
        for (QFrame* f : frames) {
            const QString cls = f->property("cls").toString();
            if ((cls == QLatin1String("reasoning-part") || cls == QLatin1String("tool-part"))
                && f->isVisible() && clicked < 5) {
                synthClick(f, QPoint(20, 10)); // header area, avoids body labels
                ++clicked;
            }
        }
    }});
    m_clickSteps.push_back({"ctx-rail-click", [this] {
        if (QListWidget* rail = m_sessionsPage->findChild<QListWidget*>()) {
            for (int i = 0; i < qMin(3, rail->count()); ++i)
                if (QListWidgetItem* it = rail->item(i))
                    synthClick(rail->viewport(), rail->visualItemRect(it).center());
        }
    }});
    m_clickSteps.push_back({"ctx-expand-all", [this] {
        for (QPushButton* b : m_sessionsPage->findChildren<QPushButton*>())
            if (b->isVisible() && b->text() == QStringLiteral("展开全部"))
                synthClickCenter(b);
    }});
    m_clickSteps.push_back({"ctx-collapse-all", [this] {
        for (QPushButton* b : m_sessionsPage->findChildren<QPushButton*>())
            if (b->isVisible() && b->text() == QStringLiteral("收拢全部"))
                synthClickCenter(b);
    }});

    // ── 6. errors page: rows + trace restore ──
    m_clickSteps.push_back({"goto-errors", [this] {
        navigateTo(QStringLiteral("errors"));
    }});
    for (int r = 0; r < 4; ++r) {
        const int row = r;
        m_clickSteps.push_back({QStringLiteral("errors-row-%1").arg(row), [this, row] {
            const auto tables = m_errorsPage->findChildren<QTableWidget*>();
            for (QTableWidget* t : tables) {
                if (t->isVisible() && row < t->rowCount() && t->columnCount() > 0) {
                    const QRect rect = t->visualItemRect(t->item(row, 0));
                    if (!rect.isNull())
                        synthClick(t->viewport(), rect.center());
                }
            }
        }});
    }
    m_clickSteps.push_back({"errors-trace-go", [this] {
        for (QPushButton* b : m_errorsPage->findChildren<QPushButton*>())
            if (b->isVisible() && b->text() == QStringLiteral("还原"))
                synthClickCenter(b);
    }});

    // ── 7. raw page: query ──
    m_clickSteps.push_back({"goto-raw", [this] {
        navigateTo(QStringLiteral("raw"));
    }});
    m_clickSteps.push_back({"raw-go", [this] {
        for (QPushButton* b : m_rawPage->findChildren<QPushButton*>())
            if (b->isVisible() && b->text() == QStringLiteral("查询"))
                synthClickCenter(b);
    }});

    // ── 8. agents page: expand rows + open buttons ──
    m_clickSteps.push_back({"goto-agents", [this] {
        navigateTo(QStringLiteral("agents"));
    }});
    m_clickSteps.push_back({"agents-rows", [this] {
        if (QTreeWidget* tree = m_agentsPage->findChild<QTreeWidget*>()) {
            for (int i = 0; i < qMin(5, tree->topLevelItemCount()); ++i) {
                QTreeWidgetItem* it = tree->topLevelItem(i);
                const QRect vr = tree->visualItemRect(it);
                if (!vr.isNull())
                    synthClick(tree->viewport(), vr.center());
            }
        }
    }});
    m_clickSteps.push_back({"agents-open-btn", [this] {
        for (QPushButton* b : m_agentsPage->findChildren<QPushButton*>())
            if (b->isVisible() && b->text() == QStringLiteral("打开 →")) {
                synthClickCenter(b);
                return; // one deep link is enough
            }
    }});

    // ── 9. theme toggle ×2 (round trip) + checkpoint button ──
    m_clickSteps.push_back({"theme-toggle-1", [this] { synthClickCenter(m_themeBtn); }});
    m_clickSteps.push_back({"theme-toggle-2", [this] { synthClickCenter(m_themeBtn); }});
    m_clickSteps.push_back({"checkpoint-btn", [this] {
        // same as the topbar button (force checkpoint on the real db)
        onCheckpointClicked();
    }});

    // ── 10. resize window ──
    const QSize resizeSizes[] = {{1500, 920}, {900, 600}, {1700, 1000}, {1100, 700}};
    for (int i = 0; i < 4; ++i) {
        const QSize sz = resizeSizes[i];
        m_clickSteps.push_back({QStringLiteral("resize-%1").arg(i), [this, sz] {
            resize(sz);
        }});
    }

    // ── 11. random fuzz: 300 single clicks + wheel at random positions ──
    for (int i = 0; i < 300; ++i) {
        m_clickSteps.push_back({QStringLiteral("fuzz-%1").arg(i), [this, i] {
            // deterministic sequence, mixed nav every 25 clicks
            if (i % 25 == 0) {
                static const QStringList pages = {"overview", "sessions", "agents",
                                                  "errors", "raw", "how"};
                navigateTo(pages.at((i / 25) % pages.size()));
                return;
            }
            QWidget* central = centralWidget();
            const int w = qMax(1, central->width());
            const int h = qMax(1, central->height());
            const int px = (i * 7919) % w;
            const int py = (i * 104729) % h;
            const QPoint p(px, py);
            QWidget* child = central->childAt(p);
            QWidget* target = child ? child : central;
            const QPoint local = target->mapFrom(central, p);
            if (i % 7 == 0)
                synthWheel(target, (i % 2) ? 120 : -120);
            else
                synthClick(target, local);
        }});
    }

    // ── 12. rapid-fire races: same row triple-clicked, tabs switched fast,
    // session clicked then page changed immediately — all trigger loadTab()
    // rebuilds whose deleteLater timing is where crashes hide ──
    for (int r = 0; r < 3; ++r) {
        const int row = r + 1;
        m_clickSteps.push_back({QStringLiteral("race-row-%1").arg(row), [this, row] {
            navigateTo(QStringLiteral("sessions"));
            QApplication::processEvents();
            if (QListWidget* list = m_sessionsPage->findChild<QListWidget*>(
                    QStringLiteral("sessionList"))) {
                for (int k = 0; k < 3; ++k) { // triple click, no event flush between
                    if (QListWidgetItem* it = list->item(row)) {
                        const QRect vr = list->visualItemRect(it);
                        synthClick(list->viewport(), vr.center());
                    }
                }
            }
        }});
    }
    m_clickSteps.push_back({"race-tabs", [this] {
        openSession(firstTestSession(), QStringLiteral("timeline"));
        QApplication::processEvents();
        if (QTabWidget* tabs = m_sessionsPage->findChild<QTabWidget*>()) {
            if (QTabBar* bar = tabs->tabBar()) {
                for (int k = 0; k < 7; ++k) // sweep all tabs back-to-back
                    synthClick(bar, bar->tabRect(k % bar->count()).center());
            }
        }
    }});
    m_clickSteps.push_back({"race-session-then-nav", [this] {
        openSession(firstTestSession(), QStringLiteral("context"));
        QApplication::processEvents();
        navigateTo(QStringLiteral("overview")); // leave mid-render
        QApplication::processEvents();
        navigateTo(QStringLiteral("sessions"));
    }});
    m_clickSteps.push_back({"race-doubleclick-list", [this] {
        if (QListWidget* list = m_sessionsPage->findChild<QListWidget*>(
                QStringLiteral("sessionList"))) {
            if (QListWidgetItem* it = list->item(2)) {
                const QPoint c = list->visualItemRect(it).center();
                for (int k = 0; k < 4; ++k) { // double-click burst on one row
                    synthClick(list->viewport(), c);
                    synthClick(list->viewport(), c);
                }
            }
        }
    }});

    // ── 13. extreme stress: theme flips interleaved with heavy clicks, and
    // rapid session switching (each loadTab rebuilds the whole tab content;
    // interleaving is the classic crash recipe for pending-delete bugs) ──
    for (int i = 0; i < 30; ++i) {
        m_clickSteps.push_back({QStringLiteral("stress-theme-click-%1").arg(i), [this, i] {
            if (i % 2 == 0)
                Theme::instance().toggle();
            // click around the (just rebuilt) sessions view while the theme
            // change is still propagating through every page's handlers
            if (QListWidget* list = m_sessionsPage->findChild<QListWidget*>(
                    QStringLiteral("sessionList"))) {
                const int row = (i * 3) % qMax(1, list->count());
                if (QListWidgetItem* it = list->item(row)) {
                    const QRect vr = list->visualItemRect(it);
                    if (!vr.isNull())
                        synthClick(list->viewport(), vr.center());
                }
            }
            const QPoint p((i * 611) % qMax(1, width()), (i * 173) % qMax(1, height()));
            if (QWidget* child = childAt(p))
                synthClick(child, child->mapFrom(this, p));
        }});
    }
    for (int i = 0; i < 20; ++i) {
        const int k = i;
        m_clickSteps.push_back({QStringLiteral("stress-session-switch-%1").arg(k), [this, k] {
            const auto sessions = DbService::instance().sessionList(30);
            if (sessions.isEmpty())
                return;
            const types::SessionRow& s = sessions.at(k % sessions.size());
            // alternate tabs to force full rebuilds of both tab flavors
            openSession(s.id, (k % 2) == 0 ? QStringLiteral("context")
                                           : QStringLiteral("timeline"));
        }});
    }

    m_clickIdx = 0;
    printf("[click] tour: %d steps\n", int(m_clickSteps.size()));
    fflush(stdout);
    QTimer::singleShot(600, this, &MainWindow::clickAdvance);
}

QString MainWindow::firstTestSession()
{
    // first interactive session from the db (stable across steps)
    static QString cached;
    if (cached.isEmpty()) {
        const auto sessions = DbService::instance().sessionList(500);
        for (const types::SessionRow& s : sessions)
            if (s.taskType == QLatin1String("interactive")) {
                cached = s.id;
                break;
            }
        if (cached.isEmpty() && !sessions.isEmpty())
            cached = sessions.first().id;
    }
    return cached;
}

void MainWindow::clickAdvance()
{
    if (m_clickIdx >= m_clickSteps.size()) {
        grab().save(m_clickDir + QLatin1Char('/') + QStringLiteral("click-final.png"));
        printf("[click] tour done, no crash\n");
        fflush(stdout);
        QApplication::quit();
        return;
    }
    const QString name = m_clickSteps.at(m_clickIdx).first;
    const auto fn = m_clickSteps.at(m_clickIdx).second;
    printf("[click] %s\n", qPrintable(name));
    fflush(stdout);
    fn();
    // flush deferred deletes + paints so the next step hits fresh state —
    // mirrors the real event loop timing between two user clicks
    QApplication::processEvents(QEventLoop::AllEvents);
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents(QEventLoop::AllEvents);
    ++m_clickIdx;
    QTimer::singleShot(120, this, &MainWindow::clickAdvance);
}
