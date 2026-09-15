// CrashHandler.cpp — see CrashHandler.h. Windows-only implementation using
// MiniDumpWriteDump (dbghelp) + a simple frame walk. The artifacts land in a
// "crash" folder next to the executable: zcode-monitor-crash-<pid>.dmd and .log.

#include "CrashHandler.h"

#ifdef Q_OS_WIN

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include <QTextStream>

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstring>

namespace {

QString crashDir()
{
    // 安装到 Program Files 后 exe 旁不可写 — 崩溃现场写到用户 AppData
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    return base + QStringLiteral("/crash");
}

QString g_lastCrashFile;

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* info)
{
    // must be async-signal-safe-ish: keep to plain Win32 + minimal CRT
    const DWORD pid = GetCurrentProcessId();
    wchar_t dirBuf[MAX_PATH];
    GetModuleFileNameW(nullptr, dirBuf, MAX_PATH);
    wchar_t* slash = wcsrchr(dirBuf, L'\\');
    if (slash)
        *slash = 0;
    wcscat_s(dirBuf, L"\\crash");
    CreateDirectoryW(dirBuf, nullptr);

    wchar_t base[64];
    swprintf_s(base, L"\\zcode-monitor-crash-%u", pid);

    wchar_t dmpPath[MAX_PATH], logPath[MAX_PATH];
    wcscpy_s(dmpPath, dirBuf); wcscat_s(dmpPath, base); wcscat_s(dmpPath, L".dmp");
    wcscpy_s(logPath, dirBuf); wcscat_s(logPath, base); wcscat_s(logPath, L".log");

    // 1) minidump with the full module + thread + exception info.
    //    dbghelp is loaded dynamically (not every MinGW toolchain ships the
    //    import lib), and MiniDumpWriteDump is thread-safe per MSDN when
    //    called from the crashing thread with a fresh handle.
    typedef BOOL (WINAPI *MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE,
                                               MINIDUMP_TYPE,
                                               PMINIDUMP_EXCEPTION_INFORMATION,
                                               PMINIDUMP_USER_STREAM_INFORMATION,
                                               PMINIDUMP_CALLBACK_INFORMATION);
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (dbghelp) {
        auto writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(
            reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
        if (writeDump) {
            HANDLE file = CreateFileW(dmpPath, GENERIC_WRITE, 0, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION mei;
                mei.ThreadId = GetCurrentThreadId();
                mei.ExceptionPointers = info;
                mei.ClientPointers = FALSE;
                writeDump(GetCurrentProcess(), pid, file,
                          MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory
                                        | MiniDumpScanMemory),
                          &mei, nullptr, nullptr);
                CloseHandle(file);
            }
        }
    }

    // 2) textual crash line (exception code + address) for a quick read
    HANDLE log = CreateFileW(logPath, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log != INVALID_HANDLE_VALUE && info && info->ExceptionRecord) {
        char line[256];
        const int n = snprintf(line, sizeof(line),
                                "code=0x%08lX addr=%p thread=%lu\n",
                                (unsigned long)info->ExceptionRecord->ExceptionCode,
                                info->ExceptionRecord->ExceptionAddress,
                                (unsigned long)GetCurrentThreadId());
        DWORD written = 0;
        WriteFile(log, line, DWORD(n), &written, nullptr);
        CloseHandle(log);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

namespace CrashHandler {

bool install()
{
    SetUnhandledExceptionFilter(unhandledFilter);
    // make sure the folder exists up front so the handler only creates files
    QDir().mkpath(crashDir());
    return true;
}

QString lastCrashFile()
{
    // newest .log under <exe>/crash
    const QDir d(crashDir());
    const auto logs = d.entryList({QStringLiteral("*.log")}, QDir::Files, QDir::Time);
    return logs.isEmpty() ? QString() : d.filePath(logs.first());
}

} // namespace CrashHandler

#else // !Q_OS_WIN

namespace CrashHandler {
bool install() { return false; }
QString lastCrashFile() { return QString(); }
} // namespace CrashHandler

#endif
