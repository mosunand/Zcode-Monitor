// Paths.cpp — see Paths.h.

#include "Paths.h"

#include <QDir>
#include <QFile>
#include <QtGlobal>

namespace Paths {

static QString underHome(const QString& tail)
{
    return QDir::homePath() + QStringLiteral("/.zcode/cli/") + tail;
}

QString dbPath()
{
    const QByteArray env = qgetenv("ZCODE_DB");
    if (!env.isEmpty()) return QDir::cleanPath(QString::fromLocal8Bit(env));
    return underHome(QStringLiteral("db/db.sqlite"));
}

QString logDir()
{
    const QByteArray env = qgetenv("ZCODE_LOG_DIR");
    if (!env.isEmpty()) return QDir::cleanPath(QString::fromLocal8Bit(env));
    return underHome(QStringLiteral("log"));
}

QString agentsDir()
{
    return underHome(QStringLiteral("agents"));
}

QString execDir()
{
    return underHome(QStringLiteral("exec"));
}

} // namespace Paths
