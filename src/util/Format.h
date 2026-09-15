#pragma once
// Format.h — port of the shared formatters in public/app.js.

#include <QString>
#include <QtGlobal>

namespace Format {

// NaN (used as "null") renders as "—" exactly like the JS `n == null` path.
QString fmtNum(double n);
// 中文数字单位（万/亿/兆）—— 总 token 卡专用，避免 M/k 缩写
QString fmtNumCn(double n);
QString fmtInt(double n);            // locale-grouped integer, NaN → "—"
QString fmtInt64(qint64 n);
QString fmtMs(double ms);           // 123ms / 1.2s / 12s
QString fmtDur(double sec);         // 45s / 2m30s / 1h05m
QString fmtTime(qint64 ms);         // today → HH:mm:ss, else M/D HH:mm
QString fmtTimeFull(qint64 ms);     // locale date-time
QString relTime(qint64 ms);          // "3m 前" style
// 图表 x 轴刻度专用两行标签：第一行 HH:mm（无秒），第二行 月/日
QString fmtTimeTick(qint64 ms);
QString shortId(const QString& id, int n = 12);
QString truncateWs(const QString& s, int n); // collapse whitespace + truncate with …

// token-speed tier: 0 = none (null/non-finite), 1 = red (<30), 2 = yellow (30–80), 3 = green (>80)
int speedTier(double tps);

// web speedClass: '' for null; maps tier → qss class name used by chips
inline bool isFinite(double v) { return v == v && v != HUGE_VAL && v != -HUGE_VAL; }

} // namespace Format
