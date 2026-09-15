// Theme.cpp — palettes ported verbatim from public/styles.css, plus the app QSS.
// QSS 使用命名令牌（@bg@ / @surface1@ …）+ replace 注入——避免位置参数链
// 在新增规则时的错位/级联污染问题。

#include "Theme.h"

#include <QApplication>
#include <QSettings>

namespace {

Palette darkPalette()
{
    Palette p;
    p.bg = QColor(0x0b, 0x0d, 0x12);
    p.surface0 = QColor(0x0f, 0x12, 0x19);
    p.surface1 = QColor(0x12, 0x15, 0x1c);
    p.surface2 = QColor(0x1a, 0x1e, 0x27);
    p.surface3 = QColor(0x1f, 0x24, 0x30);
    p.border = QColor(0x2d, 0x33, 0x42);
    p.borderSoft = QColor(0x1f, 0x24, 0x30);

    p.fg = QColor(0xfa, 0xfb, 0xfc);
    p.fg1 = QColor(0xe4, 0xe8, 0xf0);
    p.fg2 = QColor(0xbf, 0xc6, 0xd4);
    p.fg3 = QColor(0x94, 0xa3, 0xb4);
    p.fg4 = QColor(0x6c, 0x74, 0x88);
    p.fg5 = QColor(0x45, 0x4c, 0x5e);

    p.catConversation = QColor(0x38, 0xbd, 0xf8);
    p.catLlm = QColor(0x8b, 0x5c, 0xf6);
    p.catLlm2 = QColor(0xa7, 0x8b, 0xfa);
    p.catTool = QColor(0x22, 0xd3, 0xee);
    p.catTool2 = QColor(0x14, 0xb8, 0xa6);
    p.catNetwork = QColor(0x60, 0xa5, 0xfa);
    p.catUsage = QColor(0x4a, 0xde, 0x80);
    p.catPrompt = QColor(0x4a, 0xde, 0x80);
    p.catLifecycle = QColor(0x63, 0x66, 0xf1);

    p.sevOk = QColor(0x4a, 0xde, 0x80);
    p.sevWarn = QColor(0xfb, 0xbf, 0x24);
    p.sevErr = QColor(0xf8, 0x71, 0x71);
    p.sevErr2 = QColor(0xb9, 0x1c, 0x1c);

    p.accent = QColor(0x38, 0xbd, 0xf8);
    p.accent2 = QColor(0xa7, 0x8b, 0xfa);

    p.chartGrid = QColor(255, 255, 255, 13);
    p.chartGridX = QColor(255, 255, 255, 10);
    p.chartTick = QColor(0x6c, 0x74, 0x88);
    p.chartTipBg = QColor(0x1f, 0x24, 0x30);
    p.chartTipBd = QColor(0x2d, 0x33, 0x42);
    p.chartTipTitle = QColor(0xe4, 0xe8, 0xf0);
    p.chartTipBody = QColor(0xbf, 0xc6, 0xd4);
    p.chartLegend = QColor(0x94, 0xa3, 0xb4);

    p.toastBg = QColor(0x1f, 0x24, 0x30);
    return p;
}

Palette lightPalette()
{
    Palette p;
    p.bg = QColor(0xf6, 0xf8, 0xfa);
    p.surface0 = QColor(0xff, 0xff, 0xff);
    p.surface1 = QColor(0xf0, 0xf3, 0xf7);
    p.surface2 = QColor(0xe6, 0xeb, 0xf1);
    p.surface3 = QColor(0xd9, 0xe1, 0xea);
    p.border = QColor(0xcd, 0xd6, 0xe2);
    p.borderSoft = QColor(0xe6, 0xeb, 0xf1);

    p.fg = QColor(0x1f, 0x23, 0x28);
    p.fg1 = QColor(0x1f, 0x23, 0x28);
    p.fg2 = QColor(0x3b, 0x42, 0x4b);
    p.fg3 = QColor(0x59, 0x63, 0x6e);
    p.fg4 = QColor(0x81, 0x8b, 0x98);
    p.fg5 = QColor(0xb1, 0xba, 0xc4);

    p.catConversation = QColor(0x09, 0x69, 0xda);
    p.catLlm = QColor(0x82, 0x50, 0xdf);
    p.catLlm2 = QColor(0x6f, 0x42, 0xc1);
    p.catTool = QColor(0x1b, 0x8a, 0x94);
    p.catTool2 = QColor(0x0d, 0x7d, 0x6e);
    p.catNetwork = QColor(0x09, 0x69, 0xda);
    p.catUsage = QColor(0x1a, 0x7f, 0x37);
    p.catPrompt = QColor(0x1a, 0x7f, 0x37);
    p.catLifecycle = QColor(0x63, 0x66, 0xf1);

    p.sevOk = QColor(0x1a, 0x7f, 0x37);
    p.sevWarn = QColor(0x9a, 0x67, 0x00);
    p.sevErr = QColor(0xcf, 0x22, 0x2e);
    p.sevErr2 = QColor(0xa4, 0x0e, 0x26);

    p.accent = QColor(0x09, 0x69, 0xda);
    p.accent2 = QColor(0x6f, 0x42, 0xc1);

    p.chartGrid = QColor(31, 35, 40, 20);
    p.chartGridX = QColor(31, 35, 40, 15);
    p.chartTick = QColor(0x59, 0x63, 0x6e);
    p.chartTipBg = QColor(0xff, 0xff, 0xff);
    p.chartTipBd = QColor(0xcd, 0xd6, 0xe2);
    p.chartTipTitle = QColor(0x1f, 0x23, 0x28);
    p.chartTipBody = QColor(0x3b, 0x42, 0x4b);
    p.chartLegend = QColor(0x59, 0x63, 0x6e);

    p.toastBg = QColor(0xff, 0xff, 0xff);
    return p;
}

} // namespace

Theme& Theme::instance()
{
    static Theme s;
    return s;
}

Theme::Theme()
    : m_darkPal(darkPalette())
    , m_lightPal(lightPalette())
{
}

void Theme::init()
{
    QSettings settings;
    const QString t = settings.value(QStringLiteral("theme")).toString();
    m_dark = (t != QLatin1String("light"));
    m_frost = settings.value(QStringLiteral("frost"), true).toBool();
    apply();
}

void Theme::toggle()
{
    setTheme(!m_dark);
}

void Theme::setTheme(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    apply();
    emit changed(m_dark);
}

void Theme::setFrost(bool on)
{
    if (m_frost == on)
        return;
    m_frost = on;
    apply();
    emit changed(m_dark);
}

void Theme::apply()
{
    QSettings settings;
    settings.setValue(QStringLiteral("theme"), name());
    settings.setValue(QStringLiteral("frost"), m_frost);
    qApp->setStyleSheet(ThemeQss::build(pal(), m_frost));
}

QColor Theme::badgeColor(const QString& kind) const
{
    const Palette& p = pal();
    if (kind == QLatin1String("green")) return p.sevOk;
    if (kind == QLatin1String("red")) return p.sevErr;
    if (kind == QLatin1String("yellow")) return p.sevWarn;
    if (kind == QLatin1String("blue")) return p.accent;
    if (kind == QLatin1String("purple")) return p.accent2;
    if (kind == QLatin1String("teal")) return p.catTool2;
    return p.fg3; // dim
}

// ───────────────────────── application QSS ─────────────────────────
//
// 命名令牌 + replace 注入（不再使用位置 %n 参数链——规则增长时
// 位置参数会错位/级联污染）。毛玻璃模式下表面/边框带透明度。

QString ThemeQss::build(const Palette& p, bool frost)
{
    // A(): 毛玻璃时返回 rgba（带透明度），否则返回不透明颜色
    const auto A = [&frost](const QColor& c, int alpha) {
        return frost ? QStringLiteral("rgba(%1,%2,%3,%4)")
                           .arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha)
                     : c.name();
    };

    const QString cardBg = frost
        ? QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                         "stop:0 rgba(%1,%2,%3,125),stop:1 rgba(%1,%2,%3,55))")
              .arg(p.surface1.red()).arg(p.surface1.green()).arg(p.surface1.blue())
        : p.surface1.name();
    const QString cardRadius = frost ? QStringLiteral("10px") : QStringLiteral("6px");
    const bool isDark = p.fg1 != p.fg; // 暗色主题 fg1(#e4e8f0) != fg(#fafbfc)
    const QString rimTop = frost
        ? QStringLiteral("rgba(255,255,255,%1)").arg(isDark ? 30 : 170)
        : p.border.name();
    const QString rimBottom = frost
        ? QStringLiteral("rgba(0,0,0,%1)").arg(isDark ? 50 : 22)
        : p.border.name();

    QString qss = QStringLiteral(R"(
* { outline: none; }
QWidget { background-color: @bg; color: @fg1; font-family: "Segoe UI","Microsoft YaHei UI"; font-size: 10pt; }
QLabel { background: transparent; }
QToolTip { background-color: @tooltip; color: @fg1; border: 1px solid @border; padding: 5px 9px; font-size: 9pt; border-radius: 3px; }

QFrame#card { background: @cardBg; border-radius: @cardRadius; border-top: 1px solid @rimTop; border-left: 1px solid @rimTop; border-right: 1px solid @rimBottom; border-bottom: 1px solid @rimBottom; }
QFrame#topbar { background-color: @surface0; border-bottom: 1px solid @border; }
QFrame#listpane { background-color: @surface0; border-right: 1px solid @border; }

QLineEdit, QComboBox {
    background-color: @surface1; border: 1px solid @border; border-radius: 4px;
    padding: 4px 8px; color: @fg1; selection-background-color: @accent;
}
QLineEdit:hover, QComboBox:hover { border-color: @fg4; }
QLineEdit:focus, QComboBox:focus { border-color: @accent; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background-color: @surface2; border: 1px solid @border;
    selection-background-color: @surface3; color: @fg1; padding: 2px;
    outline: 0;
}

QPushButton, QToolButton {
    background-color: @surface1; border: 1px solid @border; border-radius: 4px;
    padding: 4px 11px; color: @fg1;
}
QPushButton:hover, QToolButton:hover { border-color: @accent; color: @accent; background-color: @surface2; }
QPushButton:pressed, QToolButton:pressed { background-color: @surface3; }
QPushButton:disabled { color: @fg4; border-color: @border; }
QPushButton[cls="ghost"], QToolButton[cls="ghost"] { background: transparent; }
QPushButton[cls="on"], QToolButton[cls="on"] { border-color: @accent; color: @accent; background-color: transparent; }

QToolButton[cls="nav"] {
    background: transparent; border: none; border-radius: 4px;
    padding: 5px 12px; color: @fg3; font-size: 9.5pt;
}
QToolButton[cls="nav"]:hover { background-color: @surface2; color: @fg1; }
QToolButton[cls="nav"]:checked { background-color: @surface3; color: @accent; font-weight: 600; }

QTableWidget, QTableView, QTreeWidget, QListWidget {
    background-color: transparent; border: none;
    alternate-background-color: transparent;
    selection-background-color: @surface3; selection-color: @fg1;
}
QTableWidget::item, QTableView::item { border-bottom: 1px solid @borderSoft; padding: 6px 10px; }
QTableWidget::item:selected, QTableView::item:selected { background-color: @surface3; }
QTreeWidget::item, QListWidget::item { border-bottom: 1px solid @borderSoft; padding: 2px 0; }
QTreeWidget::item:hover, QListWidget::item:hover { background-color: @surface2; }
QTreeWidget::item:selected, QListWidget::item:selected { background-color: @surface3; color: @fg1; }
QListWidget#sessionList::item { border-radius: 4px; }
QListWidget#sessionList::item:selected { border-left: 2px solid @accent; background-color: @surface2; }
QHeaderView::section {
    background-color: @surface2; color: @fg4; border: none; border-right: 1px solid @borderSoft;
    border-bottom: 1px solid @border; padding: 6px 10px;
    font-size: 9.5pt; font-weight: 600;
}
QHeaderView::section:hover { color: @fg3; }
QTableCornerButton::section { background-color: @surface2; border: none; }

QTabWidget::pane { border: none; top: -1px; }
QTabBar { background: transparent; }
QTabBar::tab {
    background: transparent; color: @fg3; padding: 7px 14px;
    border-bottom: 2px solid transparent; font-size: 9.5pt;
}
QTabBar::tab:hover { color: @fg1; }
QTabBar::tab:selected { color: @accent; border-bottom-color: @accent; font-weight: 600; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px 1px; }
QScrollBar::handle:vertical { background: @fg4; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: @fg3; }
QScrollBar::handle:vertical:pressed { background: @accent; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 1px 2px; }
QScrollBar::handle:horizontal { background: @fg4; border-radius: 4px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: @fg3; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSplitter::handle { background: @border; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:hover { background: @accent; }

QCheckBox { color: @fg2; background: transparent; spacing: 6px; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid @border; border-radius: 3px; background: @surface1; }
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; }

QProgressBar { background: @surface3; border: none; border-radius: 2px; text-align: center; color: @fg1; font-size: 8pt; }
QProgressBar::chunk { background: @accent; border-radius: 2px; }

QPlainTextEdit, QTextEdit, QTextBrowser {
    background-color: @surface1; border: 1px solid @borderSoft; border-radius: 4px; color: @fg1;
    selection-background-color: @surface3; font-family: "Consolas";
}
QPlainTextEdit:focus, QTextEdit:focus, QTextBrowser:focus { border-color: @fg4; }

QLabel#toast {
    background-color: @toast; border: 1px solid @border; border-radius: 4px;
    padding: 9px 14px; color: @fg1; font-size: 9.5pt;
}
QLabel#metaLabel { color: @fg4; font-family: "Consolas"; font-size: 8.5pt; background: transparent; }
QLabel#brand { font-weight: 600; font-size: 10pt; background: transparent; }
QLabel#h1 { font-size: 13pt; font-weight: 600; background: transparent; }
QLabel#h2 { font-size: 9.5pt; font-weight: 600; color: @fg1; background: transparent; }
QLabel#cardTitle {
    font-size: 8.5pt; color: @fg3; background: transparent; border: none;
}
QLabel#detailTitle { font-size: 11pt; font-weight: 600; background: transparent; }
QLabel#detailSub { color: @fg4; font-family: "Consolas"; font-size: 8.5pt; background: transparent; }
QLabel#empty { color: @fg4; padding: 26px; background: transparent; qproperty-alignment: AlignCenter; }
QLabel#errorCard { color: @fg1; background: transparent; }
QLabel[cls="mono"] { font-family: "Consolas"; }

QLabel[cls="faint"], QWidget[cls="faint"] { color: @fg4; background: transparent; }
QLabel[cls="muted"], QWidget[cls="muted"] { color: @fg3; background: transparent; }
QLabel[cls="mono-faint"] { color: @fg4; font-family: "Consolas"; font-size: 8.5pt; background: transparent; }
QLabel[cls="mono-fg4"] { color: @fg4; font-family: "Consolas"; font-size: 8pt; background: transparent; }
QLabel[cls="mono-sub"] { color: @fg3; font-family: "Consolas"; font-size: 8.5pt; background: transparent; }
QLabel[cls="kv-key"] { color: @fg4; font-size: 9pt; background: transparent; }
QLabel[cls="kv-value"] { color: @fg1; font-family: "Consolas"; font-size: 8.5pt; background: transparent; }
QLabel[cls="sectionSub"] { color: @fg4; font-size: 8.5pt; background: transparent; }
QLabel[cls="value-fg"] { font-size: 19pt; font-weight: 600; color: @fg; background: transparent; }
QLabel[cls="kpi-label"] { color: @fg4; font-size: 8.5pt; letter-spacing: 1px; background: transparent; }
QLabel[cls="kpi-delta"] { color: @fg3; font-size: 9pt; background: transparent; }
QLabel[cls="accent"] { color: @accent; background: transparent; }
QLabel[cls="role-user"] { font-weight: 600; font-size: 8pt; letter-spacing: .5px; color: @catConversation; background: transparent; }
QLabel[cls="role-assistant"] { font-weight: 600; font-size: 8pt; letter-spacing: .5px; color: @catLlm2; background: transparent; }
QLabel[cls="msg-num"] { color: @accent; border: 1px solid @accent; border-radius: 2px; padding: 0 5px; font-size: 8pt; background: transparent; }
QLabel[cls="think-label"] { color: @catLlm2; font-size: 7.5pt; font-weight: 600; letter-spacing: .5px; background: transparent; }
QLabel[cls="tool-label"] { color: @catTool; font-size: 8.5pt; font-weight: 600; background: transparent; }
QLabel[cls="reason-text"] { color: @fg2; font-size: 9pt; background: transparent; }
QLabel[cls="body-text"] { color: @fg1; font-size: 9.5pt; background: transparent; }
QLabel[cls="plain-mono"] { font-family: "Consolas"; font-size: 8pt; color: @fg2; background: transparent; }
QLabel[cls="plain-mono-fg1"] { font-family: "Consolas"; font-size: 8pt; color: @fg1; background: transparent; }
QLabel[cls="plain-mono-fg4"] { font-family: "Consolas"; font-size: 8pt; color: @fg4; background: transparent; }
QLabel[cls="plain-mono-8pt"] { font-family: "Consolas"; font-size: 8pt; background: transparent; color: @fg3; }
QFrame[cls="reasoning-part"] {
    background: qlineargradient(y1:0, y2:1, stop:0 @reasonTint, stop:1 rgba(0,0,0,0));
    border-left: 2px solid @catLlm2; border-radius: 0 4px 4px 0;
}
QFrame[cls="tool-part"] { background: @surface2; border: 1px solid @borderSoft; border-radius: 4px; }
QFrame[cls="msg-frame"] { background: @surface1; border: 1px solid @borderSoft; border-radius: 6px; }
QWidget[cls="msg-head"] { background: @surface2; border-radius: 6px 6px 0 0; }
QFrame#diagram {
    background-color: @bg; border: 1px solid @border; border-radius: 6px;
    font-family: "Consolas"; font-size: 9pt; color: @fg3;
}
QLabel[cls="example-box"] {
    background: @bg; border: 1px solid @borderSoft; border-radius: 4px;
    font-family: "Consolas"; font-size: 8pt; color: @fg3;
}
QLabel[cls="how-title"] { color: @accent; font-size: 10pt; font-weight: 600; background: transparent; }
QLabel[cls="how-body"] { color: @fg2; font-size: 9.5pt; background: transparent; }
)");

    // ── 命名令牌注入 ──
    const QString tint = QStringLiteral("rgba(%1,%2,%3,18)")
                             .arg(p.catLlm2.red()).arg(p.catLlm2.green()).arg(p.catLlm2.blue());

    struct Token {
        const char* name;
        QString value;
    };
    const Token tokens[] = {
        {"@bg",            A(p.bg, 200)},
        {"@surface0",      A(p.surface0, 120)},
        {"@surface1",      A(p.surface1, 115)},
        {"@surface2",      A(p.surface2, 95)},
        {"@surface3",      A(p.surface3, 135)},
        {"@border",        A(p.border, 85)},
        {"@borderSoft",    A(p.borderSoft, 55)},
        {"@tooltip",       A(p.surface3, 235)},
        {"@toast",         A(p.toastBg, 190)},
        {"@fg",            p.fg.name()},
        {"@fg1",           p.fg1.name()},
        {"@fg2",           p.fg2.name()},
        {"@fg3",           p.fg3.name()},
        {"@fg4",           p.fg4.name()},
        {"@accent",        p.accent.name()},
        {"@catConversation", p.catConversation.name()},
        {"@catLlm2",       p.catLlm2.name()},
        {"@catTool",       p.catTool.name()},
        {"@reasonTint",    tint},
        {"@cardBg",        cardBg},
        {"@cardRadius",    cardRadius},
        {"@rimTop",        rimTop},
        {"@rimBottom",     rimBottom},
    };
    // 按令牌长度降序替换：@fg1 必须先于 @fg（否则前缀令牌会互相吞噬）
    Token sorted[sizeof(tokens) / sizeof(tokens[0])];
    const int n = int(sizeof(tokens) / sizeof(tokens[0]));
    for (int i = 0; i < n; ++i)
        sorted[i] = tokens[i];
    std::sort(sorted, sorted + n, [](const Token& a, const Token& b) {
        return QByteArray(a.name).size() > QByteArray(b.name).size();
    });
    for (int i = 0; i < n; ++i)
        qss.replace(QLatin1String(sorted[i].name), sorted[i].value);

    return qss;
}
