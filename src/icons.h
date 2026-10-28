// Line icons for the server window: the same Lucide shapes the web pages use
// (public/icons.js), drawn with GDI+ on the design system's 24px grid with a
// 2px stroke and round ends.
#pragma once
#include <windows.h>

namespace on {

enum class Icon {
    Check,
    Copy,
    Printer,
    External,
    Info,
    Alert,
    Send,
    Bell,
    Globe,
    More,
};

// Draws `icon` filling a size-by-size box at (x, y) in `color`. GDI+ must
// already be started.
void drawIcon(HDC dc, Icon icon, int x, int y, int size, COLORREF color);

}  // namespace on
