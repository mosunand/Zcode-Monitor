#pragma once
// Frost.h — Windows 11 acrylic/frosted-glass backdrop for the main window.
// DWM draws a blurred, wallpaper-tinted sheet behind the window; the app's
// QSS surfaces turn semi-transparent so content floats on frosted panels.
// Gracefully degrades: on systems without DWMWA_SYSTEMBACKDROP_TYPE this
// reports failure and the caller falls back to the opaque theme.

class QWidget;

namespace Frost {

// apply (on=true) or remove (on=false) the acrylic backdrop.
// darkTheme 选择玻璃底色调（深/浅）。Returns true when accepted.
bool apply(QWidget* window, bool on, bool darkTheme);

} // namespace Frost
