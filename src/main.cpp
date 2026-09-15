// main.cpp — entry point.
//   --selftest           headless data-layer verification (exit code 0/1)
//   --screenshot [dir]   walk every page/tab, save PNGs, quit (default dir: .)

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>

#include "selftest/SelfTest.h"
#include "util/CrashHandler.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
    const QStringList args = [argv, argc]() {
        QStringList l;
        for (int i = 1; i < argc; ++i)
            l << QString::fromLocal8Bit(argv[i]);
        return l;
    }();

    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--selftest")) {
#ifdef Q_OS_WIN
            // WIN32 subsystem has no console of its own. If stdout is already
            // usable (pipe/disk — e.g. launched from a shell) just print into
            // it; otherwise attach to the parent's console if there is one.
            const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            if (!h || GetFileType(h) == FILE_TYPE_UNKNOWN) {
                if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                    FILE* unused = nullptr;
                    freopen_s(&unused, "CONOUT$", "w", stdout);
                    freopen_s(&unused, "CONOUT$", "w", stderr);
                }
            }
#endif
            QCoreApplication app(argc, argv);
            const int code = SelfTest::run();
            fflush(stdout);
            fflush(stderr);
            return code;
        }
    }

    // --screenshot [dir]: the optional argument must not be another flag
    QString screenshotDir;
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--screenshot")) {
            screenshotDir = QStringLiteral(".");
            if (i + 1 < args.size() && !args.at(i + 1).startsWith(QLatin1Char('-')))
                screenshotDir = args.at(i + 1);
            break;
        }
    }

    // --clicktest [dir] [real]: scripted interaction fuzz (crash reproduction).
    // "real" drives the OS cursor via SendInput — the hardware input path.
    QString clickDir;
    bool clickReal = false;
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--clicktest")) {
            clickDir = QStringLiteral(".");
            if (i + 1 < args.size() && !args.at(i + 1).startsWith(QLatin1Char('-')))
                clickDir = args.at(i + 1);
            if (args.contains(QLatin1String("real")))
                clickReal = true;
            break;
        }
    }

    QApplication app(argc, argv);
    // On Windows GUI-subsystem apps Qt routes qInfo/qWarning to
    // OutputDebugString instead of stderr — invisible when launched from a
    // shell. Install a handler that writes to stderr so diagnostics and Qt
    // warnings are always visible when output is redirected.
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& ctx,
                              const QString& msg) {
        const char* level = "info";
        switch (type) {
        case QtDebugMsg: level = "debug"; break;
        case QtInfoMsg: level = "info"; break;
        case QtWarningMsg: level = "warning"; break;
        case QtCriticalMsg: level = "critical"; break;
        case QtFatalMsg: level = "fatal"; break;
        }
        QByteArray line = QByteArray("[qt:") + level + "] " + msg.toUtf8() + '\n';
        if (ctx.file)
            line += QByteArray("  (") + ctx.file + ':' + QByteArray::number(ctx.line) + ")\n";
        fwrite(line.constData(), 1, line.size(), stderr);
        fflush(stderr);
    });
    // crash capture FIRST: any later unhandled exception writes a minidump +
    // log under <exe dir>/crash/ so user reports come with a stack
    CrashHandler::install();
    // GUI assembly lives in ui/MainWindow.cpp and is wired in runGui().
    extern int runGui(QApplication& app, const QString& screenshotDir,
                      const QString& clickDir, bool clickReal);
    return runGui(app, screenshotDir, clickDir, clickReal);
}
