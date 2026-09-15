#pragma once
// Paths.h — resolution of the ~/.zcode/cli data locations.
// Environment overrides keep the same names as the original:
//   ZCODE_DB, ZCODE_LOG_DIR

#include <QString>

namespace Paths {

QString dbPath();     // ZCODE_DB or ~/.zcode/cli/db/db.sqlite
QString logDir();     // ZCODE_LOG_DIR or ~/.zcode/cli/log
QString agentsDir();  // ~/.zcode/cli/agents
QString execDir();    // ~/.zcode/cli/exec

} // namespace Paths
