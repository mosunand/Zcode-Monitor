// RawService.cpp — see RawService.h.

#include "RawService.h"

#include <QRegularExpression>
#include <QSqlRecord>

#include "DbService.h"

const QStringList& RawService::allowedTables()
{
    static const QStringList tables = {
        QStringLiteral("session"),       QStringLiteral("message"),
        QStringLiteral("part"),         QStringLiteral("model_usage"),
        QStringLiteral("tool_usage"),    QStringLiteral("turn_usage"),
        QStringLiteral("session_entry"), QStringLiteral("session_target"),
        QStringLiteral("session_input"), QStringLiteral("session_task_link"),
        QStringLiteral("input_history"), QStringLiteral("local_setting"),
        QStringLiteral("todo"),          QStringLiteral("permission"),
        QStringLiteral("schema_migration"), QStringLiteral("workflow_definition"),
        QStringLiteral("workflow_run"),  QStringLiteral("workflow_activity"),
        QStringLiteral("workflow_event"),
    };
    return tables;
}

const QStringList& RawService::allowedOrderColumns()
{
    static const QStringList cols = {
        QStringLiteral("started_at"), QStringLiteral("time_created"),
        QStringLiteral("time_updated"), QStringLiteral("id"),
        QStringLiteral("sequence"),   QStringLiteral("position"),
    };
    return cols;
}

bool RawService::validateWhere(const QString& where, QString* errOut)
{
    if (where.isEmpty())
        return true;
    // over-long where: the original silently DROPS the filter and returns the
    // whole table; we reject instead — returning unfiltered data by surprise
    // is worse than an explicit error (intentional hardening)
    if (where.size() >= 500) {
        if (errOut) *errOut = QStringLiteral("invalid where clause");
        return false;
    }
    static const QRegularExpression bad(
        QStringLiteral(";|--|/\\*|\\*/"), QRegularExpression::CaseInsensitiveOption);
    if (bad.match(where).hasMatch()) {
        if (errOut) *errOut = QStringLiteral("invalid where clause");
        return false;
    }
    return true;
}

types::RawResult RawService::query(const QString& table, int limit, int offset,
                                    const QString& order, bool desc, const QString& where)
{
    types::RawResult r;
    r.table = table;
    r.limit = limit;
    r.offset = offset;

    if (!allowedTables().contains(table)) {
        r.error = QStringLiteral("table '%1' not allowed").arg(table);
        return r;
    }
    QString whereErr;
    if (!validateWhere(where, &whereErr)) {
        r.error = whereErr;
        return r;
    }

    limit = qBound(1, limit, 1000);
    r.limit = limit; // report the clamped value actually used
    const QString whereSql = where.isEmpty()
        ? QString() : QStringLiteral("WHERE ") + where;
    const QString orderSql = (allowedOrderColumns().contains(order))
        ? QStringLiteral("ORDER BY ") + order + (desc ? QStringLiteral(" DESC")
                                                       : QStringLiteral(" ASC"))
        : QString();
    auto& db = DbService::instance();

    // count first (matches the original's second query for `count`)
    db.run(QStringLiteral("SELECT COUNT(*) AS n FROM ") + table + QLatin1Char(' ') + whereSql,
           {}, [&r](QSqlQuery& q) {
               if (q.next())
                   r.count = q.value(0).toLongLong();
           });

    db.run(QStringLiteral("SELECT * FROM ") + table + QLatin1Char(' ') + whereSql
               + QLatin1Char(' ') + orderSql + QStringLiteral(" LIMIT :limit OFFSET :offset"),
           {{QStringLiteral(":limit"), limit}, {QStringLiteral(":offset"), offset}},
           [&r](QSqlQuery& q) {
               const QSqlRecord rec = q.record();
               bool colsSet = false;
               while (q.next()) {
                   QVariantMap row;
                   for (int i = 0; i < rec.count(); ++i) {
                       row.insert(rec.fieldName(i), q.value(i));
                       if (!colsSet)
                           r.columns << rec.fieldName(i);
                   }
                   colsSet = true;
                   r.rows.push_back(row);
               }
           });

    r.ok = db.lastQueryOk();
    if (!r.ok)
        r.error = db.lastError();
    return r;
}
