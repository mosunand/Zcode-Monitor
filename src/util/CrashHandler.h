#pragma once
// CrashHandler.h — unhandled-exception capture on Windows: writes a
// minidump + a textual backtrace + the Qt log tail next to the exe, so a
// user-reported "it just crashes" comes with a reproducible stack.

#include <QString>

namespace CrashHandler {
// installs the filter; returns false when unsupported (non-Windows)
bool install();
// where the last crash artifacts were written (empty when none)
QString lastCrashFile();
} // namespace CrashHandler
