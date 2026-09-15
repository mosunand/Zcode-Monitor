#pragma once
// MainWindow.h — app shell: topbar (status dot / brand / nav / meta /
// checkpoint / theme toggle) + stacked pages + toast + 5s health loop.
// Port of index.html's shell and app.js's healthLoop / theme / checkpoint.

#include <QMainWindow>
#include <QTimer>

#include <functional>

class QButtonGroup;
class QLabel;
class QStackedWidget;
class QToolButton;
class RuntimeWatchdog;
class Dot;
class OverviewPage;
class SessionsPage;
class AgentsPage;
class ErrorsPage;
class RawPage;
class HowPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // navigation (replaces the hash router): page = overview|sessions|agents|errors|raw|how
    void navigateTo(const QString& page);
    // deep link: #/sessions/{id}/{tab}
    void openSession(const QString& sessionId, const QString& tab);

    // automated visual verification: walk every page/tab, save PNGs to dir,
    // then quit. Used by `zcode-monitor --screenshot [dir]`.
    void screenshotTour(const QString& outDir);

    // scripted interaction fuzz: click/drag/scroll through every control plus
    // random clicks, then quit. Used by `zcode-monitor --clicktest [dir] [real]`
    // to reproduce user-reported click crashes under a debugger. real=true
    // drives the OS cursor via SendInput (hardware input path).
    void clickTour(const QString& outDir, bool realInput = false);

    // 毛玻璃（DWM 亚克力背景）应用/移除 —— 跟随 Theme 的 frost 开关
    void applyFrost();

public slots:
    void showToast(const QString& msg);

protected:
    void resizeEvent(QResizeEvent* e) override;

private slots:
    void healthTick();
    void onCheckpointClicked();
    void onThemeClicked();

private:
    QWidget* buildTopbar();
    void updateThemeButton();
    void renderHealthMeta(bool dbOk); // GUI-thread half of healthTick
    void tourAdvance();  // screenshotTour state machine step
    void clickAdvance(); // clickTour state machine step
    QString firstTestSession(); // stable session id used by the click tour

    QStackedWidget* m_stack = nullptr;
    QButtonGroup* m_navGroup = nullptr;
    Dot* m_statusDot = nullptr;
    QLabel* m_meta = nullptr;
    QToolButton* m_themeBtn = nullptr;
    QToolButton* m_checkpointBtn = nullptr;
    QLabel* m_toast = nullptr;
    QTimer m_healthTimer;
    QTimer m_toastTimer;
    bool m_lastDbOk = true; // last probe result, re-applied to the dot on theme flips
    bool m_checkpointBusy = false; // re-entry guard for the checkpoint button

    // screenshot tour state
    struct TourStep {
        QString name;
        int action = 0;
        QString page, sessionId, tab;
    };
    QVector<TourStep> m_tourSteps;
    int m_tourIndex = 0;
    QString m_tourDir;

    // click tour state: named interaction steps, run 120ms apart
    QVector<QPair<QString, std::function<void()>>> m_clickSteps;
    int m_clickIdx = 0;
    QString m_clickDir;

    RuntimeWatchdog* m_watchdog = nullptr;
    OverviewPage* m_overviewPage = nullptr;
    SessionsPage* m_sessionsPage = nullptr;
    AgentsPage* m_agentsPage = nullptr;
    ErrorsPage* m_errorsPage = nullptr;
    RawPage* m_rawPage = nullptr;
    HowPage* m_howPage = nullptr;
};

// GUI assembly entry used by main.cpp (screenshotDir: run the screenshot tour
// instead of the interactive loop when non-null)
int runGui(QApplication& app, const QString& screenshotDir = QString());
