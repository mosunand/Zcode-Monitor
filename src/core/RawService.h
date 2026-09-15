#pragma once
// RawService.h — port of server/routes/raw.js (raw table viewer).
// Same allowlists (tables + order columns) and the same free-text `where`
// fragment capability with the same destructive-token guard.

#include <QString>
#include <QStringList>

#include "Types.h"

class RawService {
public:
    static const QStringList& allowedTables();
    static const QStringList& allowedOrderColumns();

    // limit ≤ 1000. where: optional SQL fragment (max 500 chars, rejects ; -- /* */)
    static types::RawResult query(const QString& table, int limit = 100, int offset = 0,
                                   const QString& order = QString(), bool desc = false,
                                   const QString& where = QString());

    static bool validateWhere(const QString& where, QString* errOut);
};
