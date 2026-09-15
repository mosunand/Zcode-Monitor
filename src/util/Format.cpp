// Format.cpp — see Format.h. Faithful port of the app.js formatter semantics.

#include "Format.h"

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>

#include <cmath>

namespace Format {

QString fmtNum(double n)
{
    if (!(n == n)) return QStringLiteral("—"); // NaN == null
    const double abs = qAbs(n);
    if (abs >= 1e9) return QString::number(n / 1e9, 'f', 2) + 'B';
    if (abs >= 1e6) return QString::number(n / 1e6, 'f', 2) + 'M';
    if (abs >= 1e3) return QString::number(n / 1e3, 'f', 1) + 'k';
    // JS String(n) prints integers without decimals
    if (std::floor(n) == n) return QString::number((qint64)n);
    return QString::number(n);
}

QString fmtNumCn(double n)
{
    if (!(n == n)) return QStringLiteral("—");
    const double abs = qAbs(n);
    if (abs >= 1e12) return QString::number(n / 1e12, 'f', 2) + QStringLiteral("兆");
    if (abs >= 1e8)  return QString::number(n / 1e8,  'f', 2) + QStringLiteral("亿");
    if (abs >= 1e4)  return QString::number(n / 1e4,  'f', 1) + QStringLiteral("万");
    if (std::floor(n) == n) return QString::number((qint64)n);
    return QString::number(n);
}

QString fmtInt(double n)
{
    if (!(n == n)) return QStringLiteral("—"); // NaN guard: (qint64)qQNaN() is UB
    return QLocale().toString((qint64)n);
}

QString fmtInt64(qint64 n)
{
    return QLocale().toString(n);
}

QString fmtMs(double ms)
{
    if (!(ms == ms)) return QStringLiteral("—");
    if (ms < 1000) return QString::number(qRound(ms)) + QStringLiteral("ms");
    const int decimals = ms < 10000 ? 1 : 0;
    return QString::number(ms / 1000.0, 'f', decimals) + 's';
}

QString fmtDur(double sec)
{
    if (!(sec == sec)) return QStringLiteral("—");
    if (sec < 60) return QString::number(qRound(sec)) + 's';
    if (sec < 3600) {
        const int m = int(sec / 60);
        const int s = qRound(std::fmod(sec, 60.0));
        return QString::number(m) + 'm' + (s ? QString::number(s) + 's' : QString());
    }
    const int h = int(sec / 3600);
    const int m = int(std::fmod(sec, 3600.0) / 60);
    return QString::number(h) + 'h' + QString::number(m) + 'm';
}

QString fmtTime(qint64 ms)
{
    if (ms <= 0) return QStringLiteral("—");
    const QDateTime d = QDateTime::fromMSecsSinceEpoch(ms);
    if (!d.isValid()) return QStringLiteral("—");
    const QDateTime now = QDateTime::currentDateTime();
    if (d.date() == now.date())
        return d.time().toString(QStringLiteral("HH:mm:ss"));
    return QString::number(d.date().month()) + '/' + QString::number(d.date().day())
         + ' ' + d.time().toString(QStringLiteral("HH:mm"));
}

QString fmtTimeTick(qint64 ms)
{
    if (ms <= 0) return QStringLiteral("—");
    const QDateTime d = QDateTime::fromMSecsSinceEpoch(ms);
    if (!d.isValid()) return QStringLiteral("—");
    // 第一行时刻（无秒），第二行月/日 —— 两行都不会互相挤压
    return d.time().toString(QStringLiteral("HH:mm")) + QLatin1Char('\n')
         + QString::number(d.date().month()) + QLatin1Char('/')
         + QString::number(d.date().day());
}

QString fmtTimeFull(qint64 ms)
{
    if (ms <= 0) return QStringLiteral("—");
    const QDateTime d = QDateTime::fromMSecsSinceEpoch(ms);
    if (!d.isValid()) return QStringLiteral("—");
    return QLocale().toString(d, QLocale::ShortFormat);
}

QString relTime(qint64 ms)
{
    if (ms <= 0) return QStringLiteral("—");
    const double diff = (QDateTime::currentMSecsSinceEpoch() - ms) / 1000.0;
    if (diff < 60) return QString::number(qRound(diff)) + QStringLiteral("s 前");
    if (diff < 3600) return QString::number(int(diff / 60)) + QStringLiteral("m 前");
    if (diff < 86400) return QString::number(int(diff / 3600)) + QStringLiteral("h 前");
    return QString::number(int(diff / 86400)) + QStringLiteral("d 前");
}

QString shortId(const QString& id, int n)
{
    if (id.isEmpty()) return QStringLiteral("—");
    return id.left(n);
}

QString truncateWs(const QString& s, int n)
{
    if (s.isEmpty()) return QString();
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QString t = s;
    t.replace(ws, QStringLiteral(" "));
    t = t.trimmed();
    if (t.size() > n) return t.left(n) + QStringLiteral("…");
    return t;
}

int speedTier(double tps)
{
    if (!(tps == tps) || tps == HUGE_VAL || tps == -HUGE_VAL) return 0;
    if (tps < 30) return 1;
    if (tps <= 80) return 2;
    return 3;
}

} // namespace Format
