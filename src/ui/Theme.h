#pragma once
// Theme.h — dual-theme system, port of the CSS design tokens in styles.css.
// Dark (default) / light palettes + a generated application stylesheet.
// Persistence: QSettings (replaces localStorage 'zc-theme').

#include <QColor>
#include <QObject>
#include <QString>

struct Palette {
    // surface stack
    QColor bg, surface0, surface1, surface2, surface3, border, borderSoft;
    // text stack
    QColor fg, fg1, fg2, fg3, fg4, fg5;
    // category colors
    QColor catConversation, catLlm, catLlm2, catTool, catTool2, catNetwork, catUsage,
           catPrompt, catLifecycle;
    // severity
    QColor sevOk, sevWarn, sevErr, sevErr2;
    // accents
    QColor accent, accent2;
    // chart palette
    QColor chartGrid, chartGridX, chartTick, chartTipBg, chartTipBd, chartTipTitle,
           chartTipBody, chartLegend;
    QColor toastBg;
};

class Theme : public QObject {
    Q_OBJECT
public:
    static Theme& instance();

    void init();                       // load persisted theme + apply stylesheet
    void toggle();                     // topbar button / 't' shortcut
    void setTheme(bool dark);
    bool isFrost() const { return m_frost; }   // 毛玻璃（DWM 亚克力）开关
    void setFrost(bool on);                    // 切换半透明表面 + DWM 背景
    bool isDark() const { return m_dark; }
    QString name() const { return m_dark ? QStringLiteral("dark") : QStringLiteral("light"); }
    const Palette& pal() const { return m_dark ? m_darkPal : m_lightPal; }

    // badge kind → color (web .badge.green/red/yellow/blue/purple/teal/dim)
    QColor badgeColor(const QString& kind) const;

signals:
    void changed(bool dark);

private:
    Theme();
    void apply();

    bool m_dark = true;
    bool m_frost = true;
    Palette m_darkPal;
    Palette m_lightPal;
};

namespace ThemeQss {
QString build(const Palette& p, bool frost); // the full application stylesheet
}
