#include "gui.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <map>
#include <string>
#include <thread>
#include <vector>

// GDI+ uses the min and max macros that NOMINMAX switches off.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "icons.h"
#include "theme.h"
#include "util.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

namespace on {
namespace {

namespace t = theme;

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_FIXDONE = WM_APP + 2;   // wParam: Fix, lParam: 0 ok, 1 failed, 2 declined
constexpr UINT kTrayId = 1;

// The tray ids are part of the contract with the installer, which turns a
// running copy off by posting WM_COMMAND IDM_TRAY_EXIT (204) to its window -
// exactly as every 0.9 version, including Dental Flow, understands it. Never
// renumber them.
enum {
    ID_COPY = 100,
    ID_OPEN,
    ID_HIDE,
    ID_PRIMARY,
    ID_CARD,
    ID_COPY_IP,
    IDM_TRAY_SHOW = 200,
    IDM_TRAY_COPY,
    IDM_TRAY_OPEN,
    IDM_TRAY_STARTUP,
    IDM_TRAY_EXIT,      // 204
    IDM_TRAY_COPY_IP,
    ID_FIX_BASE = 300,
};

enum {
    TIMER_REFRESH = 1,
    TIMER_REVEAL,
    TIMER_COPIED,
};

constexpr UINT kRefreshMs = 5000;
constexpr long long kDeepEveryMs = 15000;
constexpr UINT kRevealMs = 380;

const wchar_t kClassName[] = L"DentalCommsServer";
// The window class before the rename, still running on some server computers.
const wchar_t kOldClassName[] = L"DentalFlowServer";
const wchar_t kRunValue[] = L"DentalComms";
// The value it was registered under before the rename from Dental Flow.
const wchar_t kOldRunValue[] = L"DentalFlow";
const wchar_t kMutexName[] = L"Local\\DentalComms.Server";
const wchar_t kShowMessage[] = L"DentalComms.ShowWindow";

// Sizes in 96-DPI pixels; S() scales them for the monitor. Everything else
// comes from the design system's tokens in theme.h.
constexpr int kWidth = 640;
constexpr int kMinHeight = 420;
constexpr int kPad = t::space6;   // the page's side margins
constexpr int kRing = 5;          // room around a button for its focus ring

enum Page { PageWelcome, PageChecking, PageReady, PageStatus };

HINSTANCE g_inst = nullptr;
HWND g_wnd = nullptr;
GuiHooks g_hooks;
Facts g_facts;
std::vector<Check> g_checks;
Page g_page = PageStatus;
size_t g_revealed = 0;
long long g_lastDeep = 0;
bool g_hadAttention = false;
UINT g_showMsg = 0;
ULONG_PTR g_gdiplus = 0;

HWND g_btnPrimary = nullptr, g_btnCopy = nullptr, g_btnCopyIp = nullptr,
     g_btnCard = nullptr, g_btnOpen = nullptr, g_btnHide = nullptr;
std::vector<HWND> g_rowButtons;   // one per row; nullptr when it has no fix
std::string g_rowSignature;
std::string g_fixNote[4];
bool g_fixRunning[4] = {false, false, false, false};

NOTIFYICONDATAW g_tray{};
bool g_trayAdded = false;
bool g_menuFromRButton = false;
HICON g_icon = nullptr, g_iconSm = nullptr;
bool g_reallyQuitting = false;

int g_dpi = 96;
int S(int px) { return MulDiv(px, g_dpi, 96); }

// Scrolling, only for a screen too short to show a whole page.
int g_scroll = 0;
int g_bodyBottom = 0;   // where the action bar starts; body buttons stop there

// ------------------------------------------------------------------ type
// One font per text style in the design system, with its line height kept
// alongside: Windows' own line spacing is tighter than the system's.
struct Font {
    t::TypeStyle style;
    HFONT h = nullptr;
};
Font g_title{t::title}, g_body{t::body}, g_bodyBold{{17, 26, 600}}, g_label{t::label},
    g_labelPlain{{15, 20, 400}}, g_overline{t::overline}, g_button{t::button},
    g_buttonBig{t::buttonBig}, g_address{t::tileLabel}, g_banner{t::banner};

Font* allFonts[] = {&g_title,   &g_body,   &g_bodyBold,  &g_label,   &g_labelPlain,
                    &g_overline, &g_button, &g_buttonBig, &g_address, &g_banner};

void deleteFonts() {
    for (Font* f : allFonts)
        if (f->h) { DeleteObject(f->h); f->h = nullptr; }
}

void makeFonts() {
    deleteFonts();
    for (Font* f : allFonts) {
        LOGFONTW l{};
        wcsncpy_s(l.lfFaceName, L"Segoe UI", _TRUNCATE);
        l.lfHeight = -S(f->style.size);
        l.lfWeight = f->style.weight;
        l.lfQuality = CLEARTYPE_QUALITY;
        l.lfCharSet = DEFAULT_CHARSET;
        f->h = CreateFontIndirectW(&l);
    }
}

// The overline style's 0.05em of letter spacing.
int overlineExtra() { return std::max(1, MulDiv(65, g_dpi, 96 * 100)); }

int textWidth(HDC dc, const Font& f, const std::wstring& s) {
    HGDIOBJ old = SelectObject(dc, f.h);
    if (&f == &g_overline) SetTextCharacterExtra(dc, overlineExtra());
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    SetTextCharacterExtra(dc, 0);
    SelectObject(dc, old);
    return sz.cx;
}

int textWidth(const Font& f, const std::wstring& s) {
    HDC dc = GetDC(g_wnd);
    const int w = textWidth(dc, f, s);
    ReleaseDC(g_wnd, dc);
    return w;
}

// Breaks text into lines that fit `width`, at spaces.
std::vector<std::wstring> wrap(HDC dc, const Font& f, const std::wstring& s, int width) {
    std::vector<std::wstring> lines;
    std::wstring cur;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(L' ', i);
        if (j == std::wstring::npos) j = s.size();
        const std::wstring word = s.substr(i, j - i);
        const std::wstring tryLine = cur.empty() ? word : cur + L" " + word;
        if (!cur.empty() && textWidth(dc, f, tryLine) > width) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = tryLine;
        }
        i = j + 1;
    }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);
    return lines;
}

// Draws one line centred in its line box, the way CSS line-height does.
void drawLine(HDC dc, const Font& f, COLORREF c, int x, int y, const std::wstring& s) {
    HGDIOBJ old = SelectObject(dc, f.h);
    if (&f == &g_overline) SetTextCharacterExtra(dc, overlineExtra());
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y + (S(f.style.line) - tm.tmHeight) / 2, s.c_str(), (int)s.size());
    SetTextCharacterExtra(dc, 0);
    SelectObject(dc, old);
}

// ---------------------------------------------------------------- shapes
// GDI+ for the shapes, so circles and corners are smooth; GDI for the text,
// which keeps ClearType.
Gdiplus::Color gc(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

void roundPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) / 2.f);
    if (r <= 0.5f) {
        p.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    const float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

struct Gfx {
    Gdiplus::Graphics g;
    explicit Gfx(HDC dc) : g(dc) {
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    }
};

constexpr COLORREF kNone = CLR_INVALID;

// A rounded shape, with an optional outline drawn inside its edge.
void shape(HDC dc, float x, float y, float w, float h, float radius, COLORREF fill,
           BYTE alpha = 255, COLORREF border = kNone, float borderW = 2) {
    Gfx gx(dc);
    if (fill != kNone) {
        Gdiplus::GraphicsPath p;
        roundPath(p, x, y, w, h, radius);
        Gdiplus::SolidBrush b(gc(fill, alpha));
        gx.g.FillPath(&b, &p);
    }
    if (border != kNone) {
        const float in = borderW / 2;
        Gdiplus::GraphicsPath q;
        roundPath(q, x + in, y + in, w - borderW, h - borderW, radius - in);
        Gdiplus::Pen pen(gc(border), borderW);
        gx.g.DrawPath(&pen, &q);
    }
}

void shape(HDC dc, const RECT& r, int radius, COLORREF fill, BYTE alpha = 255,
           COLORREF border = kNone) {
    shape(dc, (float)r.left, (float)r.top, float(r.right - r.left), float(r.bottom - r.top),
          (float)radius, fill, alpha, border, (float)S(2));
}

void circle(HDC dc, int x, int y, int d, COLORREF fill) {
    Gfx gx(dc);
    Gdiplus::SolidBrush b(gc(fill));
    gx.g.FillEllipse(&b, (float)x, (float)y, (float)d, (float)d);
}

void fillRect(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void hline(HDC dc, int x1, int x2, int y, COLORREF c) {
    fillRect(dc, RECT{x1, y, x2, y + S(1)}, c);
}

// shadow-soft: just enough to lift a card off the paper.
void softShadow(HDC dc, const RECT& r, int radius) {
    shape(dc, (float)r.left, (float)r.top + S(1), float(r.right - r.left),
          float(r.bottom - r.top), (float)radius, t::ink, 12);
    for (int i = 1; i <= 3; ++i)
        shape(dc, float(r.left - S(i) + S(1)), float(r.top + S(2)),
              float(r.right - r.left + 2 * S(i) - 2 * S(1)),
              float(r.bottom - r.top + S(i)), float(radius + S(i)), t::ink, 5);
}

// A raised card: cream, a 2px outline and shadow-soft.
void cardBg(HDC dc, const RECT& r) {
    softShadow(dc, r, S(t::radiusMd));
    shape(dc, r, S(t::radiusMd), t::surfaceRaised, 255, t::line);
}

// ---------------------------------------------------------------- facts
std::wstring staffAddressW() { return widen(staffAddress(g_facts)); }

// The number-based address, or empty when it would only repeat the other.
std::wstring ipAddressW() {
    const std::string ip = staffIpAddress(g_facts);
    return ip == staffAddress(g_facts) ? std::wstring() : widen(ip);
}

std::wstring localAddressW() {
    return L"http://localhost:" + std::to_wstring(g_facts.port);
}

void copyText(const std::wstring& w) {
    if (w.empty() || !OpenClipboard(g_wnd)) return;
    EmptyClipboard();
    const size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* dst = GlobalLock(mem)) {
            memcpy(dst, w.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

void openUrl(const std::wstring& url) {
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ------------------------------------------------------------------- icon
// Used only if the icon built into the exe can't be loaded: a denim rounded
// square with a cream speech bubble, like the real one.
HICON makeIcon(int size) {
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP colour = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!colour) {
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return nullptr;
    }
    HGDIOBJ oldBmp = SelectObject(dc, colour);
    memset(bits, 0, (size_t)size * size * 4);
    HBRUSH denim = CreateSolidBrush(t::primary);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = SelectObject(dc, denim);
    const int pad = size / 10;
    RoundRect(dc, pad, pad, size - pad, size - pad, size / 3, size / 3);
    HBRUSH cream = CreateSolidBrush(t::surfaceRaised);
    SelectObject(dc, cream);
    const int bx = size / 4, by = size / 4, bw = size / 2, bh = size / 3;
    RoundRect(dc, bx, by, bx + bw, by + bh, size / 8, size / 8);
    POINT tail[3] = {{bx + bw / 4, by + bh - 1},
                     {bx + bw / 4 + size / 8, by + bh - 1},
                     {bx + bw / 4, by + bh + size / 6}};
    Polygon(dc, tail, 3);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(denim);
    DeleteObject(cream);
    auto* px = (BYTE*)bits;
    for (int i = 0; i < size * size; ++i)
        px[i * 4 + 3] = (px[i * 4] || px[i * 4 + 1] || px[i * 4 + 2]) ? 255 : 0;
    SelectObject(dc, oldBmp);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = colour;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(colour);
    DeleteObject(mask);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return icon;
}

// The icon built into DentalComms.exe (app.rc), which Explorer and the Start
// menu already show, so the window and the tray match them.
HICON appIcon(int size) {
    if (HICON h = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, size,
                                    size, LR_DEFAULTCOLOR))
        return h;
    return makeIcon(size);
}

// ------------------------------------------------------------------- tray
std::wstring trayTip() {
    if (needsAttention(g_checks)) return L"Dental Comms needs you - click to see why";
    if (g_facts.mode != NetMode::Office) return L"Dental Comms - this computer only";
    return L"Dental Comms is running";
}

void addTray() {
    if (g_trayAdded) return;
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_wnd;
    g_tray.uID = kTrayId;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = WM_TRAY;
    g_tray.hIcon = g_iconSm;
    wcsncpy_s(g_tray.szTip, trayTip().c_str(), _TRUNCATE);
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_tray) != FALSE;
    if (g_trayAdded) {
        // Version 3 is what makes a click on a notification arrive as
        // NIN_BALLOONUSERCLICK, so "Dental Comms needs you" opens the window.
        g_tray.uVersion = NOTIFYICON_VERSION;
        Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
    }
}

void updateTray() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW d = g_tray;
    d.uFlags = NIF_TIP;
    wcsncpy_s(d.szTip, trayTip().c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &d);
}

void removeTray() {
    if (!g_trayAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &g_tray);
    g_trayAdded = false;
}

void notify(const std::wstring& title, const std::wstring& text) {
    addTray();
    if (!g_trayAdded) return;
    NOTIFYICONDATAW d = g_tray;
    d.uFlags = NIF_INFO;
    wcsncpy_s(d.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(d.szInfo, text.c_str(), _TRUNCATE);
    d.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &d);
}

// --------------------------------------------------------------- buttons
// The design system's Button, drawn by hand: primary (denim, one per screen),
// secondary (denim outline) and quiet (sand). The window each one lives in is
// a little bigger than the button itself, so the focus ring has room.
enum class Look { Primary, Secondary, Quiet };

struct Btn {
    Look look = Look::Quiet;
    bool big = false;           // the 64px main action
    COLORREF surface = t::surface;   // what it sits on
    bool hasIcon = false;
    Icon icon = Icon::Check;
    int minWidth = 0;           // so "Copied" doesn't shrink "Copy address"
    bool hover = false;
};
std::map<HWND, Btn> g_btn;

const Font& buttonFont(const Btn& b) { return b.big ? g_buttonBig : g_button; }

// The button's own width (not counting the ring margin).
int buttonWidth(HWND b) {
    auto it = g_btn.find(b);
    if (it == g_btn.end()) return S(120);
    wchar_t text[128] = {0};
    GetWindowTextW(b, text, 128);
    int w = textWidth(buttonFont(it->second), text) + 2 * S(t::space5);
    if (it->second.hasIcon) w += S(22) + S(t::space2);
    return std::max(w, it->second.minWidth);
}

LRESULT CALLBACK buttonProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            auto it = g_btn.find(h);
            if (it != g_btn.end() && !it->second.hover) {
                it->second.hover = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            auto it = g_btn.find(h);
            if (it != g_btn.end()) it->second.hover = false;
            InvalidateRect(h, nullptr, FALSE);
            break;
        }
        case WM_SETCURSOR:
            if (IsWindowEnabled(h)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_NCDESTROY:
            RemoveWindowSubclass(h, buttonProc, 1);
            g_btn.erase(h);
            break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void drawButton(const DRAWITEMSTRUCT* di) {
    auto it = g_btn.find(di->hwndItem);
    if (it == g_btn.end()) return;
    const Btn& info = it->second;
    const int W = di->rcItem.right - di->rcItem.left;
    const int H = di->rcItem.bottom - di->rcItem.top;
    const bool disabled = (di->itemState & ODS_DISABLED) != 0;
    const bool pressed = (di->itemState & ODS_SELECTED) != 0 && !disabled;
    const bool focus = (di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT);
    const bool hover = info.hover && !disabled;

    COLORREF fill = t::surfaceSunken, ink = t::ink, border = kNone;
    if (disabled) {
        fill = t::surfaceSunken;
        ink = t::inkMuted;
    } else {
        switch (info.look) {
            case Look::Primary:
                fill = hover || pressed ? t::primaryHover : t::primary;
                ink = t::onPrimary;
                break;
            case Look::Secondary:
                fill = hover || pressed ? t::primaryTint : t::surfaceRaised;
                border = t::primary;
                ink = t::primaryInk;
                break;
            case Look::Quiet:
                fill = hover || pressed ? t::surfaceHover : t::surfaceSunken;
                ink = t::ink;
                break;
        }
    }

    HDC mem = CreateCompatibleDC(di->hDC);
    HBITMAP bmp = CreateCompatibleBitmap(di->hDC, W, H);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    fillRect(mem, RECT{0, 0, W, H}, info.surface);

    // The button proper, inside the ring margin; a press shrinks it to 97%.
    float x = (float)S(kRing), y = (float)S(kRing), w = float(W - 2 * S(kRing)),
          h = float(H - 2 * S(kRing));
    if (pressed) {
        x += w * 0.015f;
        y += h * 0.015f;
        w *= 0.97f;
        h *= 0.97f;
    }
    shape(mem, x, y, w, h, (float)S(t::radiusMd), fill, 255, border, (float)S(2));
    if (focus) {
        // focus-ring: solid, 3px, with a 2px gap.
        const float gap = (float)S(2), ring = (float)S(3);
        shape(mem, (float)S(kRing) - gap - ring, (float)S(kRing) - gap - ring,
              float(W - 2 * S(kRing)) + 2 * (gap + ring), float(H - 2 * S(kRing)) + 2 * (gap + ring),
              (float)S(t::radiusMd) + gap + ring, kNone, 255, t::focusRing, ring);
    }

    wchar_t text[128] = {0};
    GetWindowTextW(di->hwndItem, text, 128);
    const Font& f = buttonFont(info);
    const int tw = textWidth(mem, f, text);
    const int iconW = info.hasIcon ? S(22) + S(t::space2) : 0;
    int cx = (W - (tw + iconW)) / 2;
    const int lineTop = (H - S(f.style.line)) / 2;
    if (info.hasIcon) {
        drawIcon(mem, info.icon, cx, (H - S(22)) / 2, S(22), ink);
        cx += iconW;
    }
    drawLine(mem, f, ink, cx, lineTop, text);

    BitBlt(di->hDC, di->rcItem.left, di->rcItem.top, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

HWND makeButton(int id, const wchar_t* text, Look look, bool big = false) {
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0,
                             10, 10, g_wnd, (HMENU)(INT_PTR)id, g_inst, nullptr);
    Btn info;
    info.look = look;
    info.big = big;
    g_btn[b] = info;
    SetWindowSubclass(b, buttonProc, 1, 0);
    return b;
}

void setIcon(HWND b, Icon icon) {
    auto it = g_btn.find(b);
    if (it == g_btn.end()) return;
    if (!it->second.hasIcon || it->second.icon != icon) {
        it->second.hasIcon = true;
        it->second.icon = icon;
        InvalidateRect(b, nullptr, FALSE);
    }
}

// Puts a button's visible shape at x, y, w, h and tells it what it sits on.
void place(HWND b, int x, int y, int w, int h, COLORREF surface) {
    auto it = g_btn.find(b);
    if (it != g_btn.end() && it->second.surface != surface) {
        it->second.surface = surface;
        InvalidateRect(b, nullptr, FALSE);
    }
    const int m = S(kRing);
    MoveWindow(b, x - m, y - m, w + 2 * m, h + 2 * m, TRUE);
}

// A button in the scrolling part of the page: hidden while any of it would
// be under the action bar or above the top.
void placeBody(HWND b, int x, int y, int w, int h, COLORREF surface) {
    place(b, x, y, w, h, surface);
    const bool visible = y >= 0 && y + h <= g_bodyBottom;
    ShowWindow(b, visible ? SW_SHOW : SW_HIDE);
}

// Only when it changes: setting a button's text repaints it, and doing that
// on every refresh would make the buttons flicker.
void setText(HWND b, const std::wstring& text) {
    wchar_t cur[160] = {0};
    GetWindowTextW(b, cur, 160);
    if (text != cur) SetWindowTextW(b, text.c_str());
}

// ---------------------------------------------------------------- layout
// One walk down the page serves measuring, painting and placing the buttons,
// so they can never disagree about where anything is.
struct Walk {
    enum Mode { Place, Paint, Measure };
    HDC dc;
    Mode mode;
    int left, right, y;

    bool painting() const { return mode == Paint; }
    bool placing() const { return mode == Place; }

    // Wrapped text, `after` px of space below it.
    void text(const Font& f, COLORREF c, const std::wstring& s, int indent = 0,
              int after = 0) {
        const int l = left + indent;
        const auto lines = wrap(dc, f, s, right - l);
        for (const auto& ln : lines) {
            if (painting()) drawLine(dc, f, c, l, y, ln);
            y += S(f.style.line);
        }
        y += S(after);
    }
};

// Measures the body, draws the background behind it, then runs it for real.
template <class Bg, class Body>
void box(Walk& w, int padX, int padY, Bg bg, Body body) {
    Walk m{w.dc, Walk::Measure, w.left + S(padX), w.right - S(padX), w.y + S(padY)};
    body(m);
    const int h = (m.y - w.y) + S(padY);
    if (w.painting()) bg(RECT{w.left, w.y, w.right, w.y + h});
    Walk in{w.dc, w.mode, w.left + S(padX), w.right - S(padX), w.y + S(padY)};
    body(in);
    w.y += h;
}

template <class Body>
void card(Walk& w, int padY, Body body) {
    box(w, t::space5, padY, [&](const RECT& r) { cardBg(w.dc, r); }, body);
}

// A section kicker in the overline style: "STAFF COMPUTERS GO TO".
void kicker(Walk& w, const std::wstring& s) {
    w.text(g_overline, t::inkMuted, s, 0, t::space2);
}

// The design system's Banner: an icon and one sentence.
enum class BannerKind { Info, Warning };
void banner(Walk& w, BannerKind kind, const std::wstring& s) {
    const COLORREF bg = kind == BannerKind::Info ? t::primaryTint : t::warningTint;
    const COLORREF edge = kind == BannerKind::Info ? kNone : t::warning;
    const COLORREF iconInk = kind == BannerKind::Info ? t::primaryInk : t::warningInk;
    box(
        w, t::space4, t::space3,
        [&](const RECT& r) { shape(w.dc, r, S(t::radiusMd), bg, 255, edge); },
        [&](Walk& c) {
            const int top = c.y;
            const int indent = S(22) + S(t::space3);
            c.text(g_banner, t::ink, s, indent, 0);
            if (c.painting())
                drawIcon(c.dc, kind == BannerKind::Info ? Icon::Info : Icon::Alert, c.left,
                         top + (S(t::banner.line) - S(22)) / 2, S(22), iconInk);
        });
}

// The top of every page: an optional step badge, the heading, and one line
// of help. `status` adds a label beside the heading on the status page.
struct Status {
    std::wstring text;
    Icon icon;
    COLORREF bg, iconInk, ink;
};
void header(Walk& w, int step, const std::wstring& title, const std::wstring& help,
            const Status* status = nullptr, const Font* helpFont = nullptr) {
    w.y += S(28);
    if (step) {
        // StepBadge: "STEP 1 OF 3".
        const std::wstring s = L"STEP " + std::to_wstring(step) + L" OF 3";
        const int bw = textWidth(g_overline, s) + 2 * S(t::space3);
        const int bh = S(t::overline.line) + 2 * S(t::space1);
        if (w.painting()) {
            shape(w.dc, RECT{w.left, w.y, w.left + bw, w.y + bh}, bh / 2, t::primaryTint);
            drawLine(w.dc, g_overline, t::primaryInk, w.left + S(t::space3), w.y + S(t::space1), s);
        }
        w.y += bh + S(t::space3);
    }
    Walk tw = w;
    int sw = 0;
    const int sh = S(32);
    if (status) {
        sw = S(t::space3) + S(18) + S(t::space2) + textWidth(g_label, status->text) +
             S(t::space3) + S(2);
        tw.right -= sw + S(t::space4);
    }
    const int titleTop = w.y;
    tw.text(g_title, t::ink, title, 0, 0);
    if (status && w.painting()) {
        const int y = titleTop + (S(t::title.line) - sh) / 2;
        const int x = w.right - sw;
        shape(w.dc, RECT{x, y, w.right, y + sh}, sh / 2, status->bg);
        drawIcon(w.dc, status->icon, x + S(t::space3), y + (sh - S(18)) / 2, S(18),
                 status->iconInk);
        drawLine(w.dc, g_label, status->ink, x + S(t::space3) + S(18) + S(t::space2),
                 y + (sh - S(t::label.line)) / 2, status->text);
    }
    w.y = tw.y;
    if (!help.empty()) {
        w.y += S(t::space1);
        w.text(helpFont ? *helpFont : g_body, t::inkMuted, help, 0, 0);
    }
    w.y += S(t::space5);
}

// A round icon with a heading and a sentence beside it (the Welcome list).
void feature(Walk& c, Icon icon, const std::wstring& title, const std::wstring& body,
             bool divider) {
    c.y += S(t::space4);
    const int top = c.y, d = S(44), indent = d + S(t::space4);
    if (c.painting()) {
        circle(c.dc, c.left, top, d, t::sand100);
        drawIcon(c.dc, icon, c.left + (d - S(22)) / 2, top + (d - S(22)) / 2, S(22), t::clay);
    }
    c.text(g_bodyBold, t::ink, title, indent, 0);
    c.text(g_body, t::inkMuted, body, indent, 0);
    c.y = std::max(c.y, top + d) + S(t::space4);
    if (divider && c.painting()) hline(c.dc, c.left + indent, c.right, c.y, t::line);
}

void stateIcon(HDC dc, int x, int y, int d, CheckState s) {
    const int i = S(18), o = (d - i) / 2;
    switch (s) {
        case CheckState::Ok:
            circle(dc, x, y, d, t::successTint);
            drawIcon(dc, Icon::Check, x + o, y + o, i, t::success);
            break;
        case CheckState::NeedsYou:
            circle(dc, x, y, d, t::warningTint);
            drawIcon(dc, Icon::Alert, x + o, y + o - S(1), i, t::warningInk);
            break;
        case CheckState::Note:
            circle(dc, x, y, d, t::primaryTint);
            drawIcon(dc, Icon::Info, x + o, y + o, i, t::primaryInk);
            break;
        case CheckState::Working:
            circle(dc, x, y, d, t::surfaceSunken);
            drawIcon(dc, Icon::More, x + o, y + o, i, t::inkMuted);
            break;
    }
}

void hideRowButtons() {
    for (HWND b : g_rowButtons)
        if (b) ShowWindow(b, SW_HIDE);
}

// Lists the first `count` rows. With `skipOk`, only the ones that are not
// already green - what is left to look at.
void rows(Walk& w, size_t count, bool skipOk) {
    bool first = true;
    for (size_t i = 0; i < g_checks.size(); ++i) {
        const Check& c = g_checks[i];
        HWND btn = i < g_rowButtons.size() ? g_rowButtons[i] : nullptr;
        const bool show = i < count && (!skipOk || c.state != CheckState::Ok);
        if (!show) {
            if (btn && w.placing()) ShowWindow(btn, SW_HIDE);
            continue;
        }
        if (!first && w.painting()) hline(w.dc, w.left + S(48), w.right, w.y, t::line);
        first = false;
        w.y += S(10);
        const int top = w.y, d = S(32), indent = S(48);
        if (w.painting()) stateIcon(w.dc, w.left, top + (S(26) - d) / 2 + S(1), d, c.state);
        w.text(g_bodyBold, t::ink, widen(c.text), indent, 0);
        if (!c.help.empty()) w.text(g_body, t::inkMuted, widen(c.help), indent, 0);
        if (btn) {
            w.y += S(t::space3);
            if (w.placing())
                placeBody(btn, w.left + indent, w.y, buttonWidth(btn), S(t::targetMin),
                          t::surfaceRaised);
            w.y += S(t::targetMin);
        }
        const std::string& note = g_fixNote[(int)c.fix];
        if (c.fix != Fix::None && !note.empty()) {
            w.y += S(t::space2);
            w.text(g_body, t::warningInk, widen(note), indent, 0);
        }
        w.y = std::max(w.y, top + d) + S(10);
    }
}

bool anyRows(size_t count, bool skipOk) {
    for (size_t i = 0; i < g_checks.size() && i < count; ++i)
        if (!skipOk || g_checks[i].state != CheckState::Ok) return true;
    return false;
}

// One address with its own Copy button, on the light blue panel.
void addressLine(Walk& c, const std::wstring& caption, const std::wstring& addr, HWND copy,
                 bool divider) {
    c.y += S(t::space4);
    const int top = c.y;
    const int bw = buttonWidth(copy);
    Walk tw = c;
    tw.right = c.right - bw - S(t::space4);
    tw.text(g_overline, t::primaryInk, caption, 0, t::space1);
    // An address broken across two lines gets typed with a space in it, so a
    // long one is set smaller rather than wrapped.
    const Font& f = textWidth(g_address, addr) <= tw.right - tw.left ? g_address : g_bodyBold;
    if (c.painting()) drawLine(c.dc, f, t::primaryInk, tw.left, tw.y, addr);
    tw.y += S(f.style.line);
    const int h = tw.y - top;
    if (c.placing())
        placeBody(copy, c.right - bw, top + (h - S(t::targetMin)) / 2, bw, S(t::targetMin),
                  t::sky100);
    c.y = std::max(tw.y, top + S(t::targetMin)) + S(t::space4);
    if (divider && c.painting()) hline(c.dc, c.left, c.right, c.y, t::sky300);
}

// The addresses staff type in: the computer's name, and its number for any
// computer that can't find it by name.
void addressPanel(Walk& w) {
    const std::wstring name = staffAddressW(), ip = ipAddressW();
    box(
        w, t::space5, 0,
        [&](const RECT& r) { shape(w.dc, r, S(t::radiusMd), t::sky100); },
        [&](Walk& c) {
            addressLine(c, ip.empty() ? L"ADDRESS" : L"COMPUTER NAME", name, g_btnCopy,
                        !ip.empty());
            if (!ip.empty()) addressLine(c, L"IP ADDRESS", ip, g_btnCopyIp, false);
        });
    if (!ip.empty()) {
        w.y += S(t::space3);
        w.text(g_body, t::inkMuted,
               L"Either one works. If the name doesn't open, use the IP address.",
               0, 0);
    }
}

// Print setup card / Open Dental Comms, side by side in the page.
void actionRow(Walk& w, bool withCard) {
    if (w.placing()) {
        int x = w.left;
        if (!withCard) ShowWindow(g_btnCard, SW_HIDE);
        for (HWND b : {g_btnCard, g_btnOpen}) {
            if (b == g_btnCard && !withCard) continue;
            const int bw = buttonWidth(b);
            placeBody(b, x, w.y, bw, S(t::targetMin), t::surface);
            x += bw + S(t::space3);
        }
    }
    w.y += S(t::targetMin);
}

bool bigFooter() { return g_page != PageStatus; }

int footerHeight() {
    return (bigFooter() ? S(t::targetBig) : S(t::targetMin)) + 2 * S(t::space4);
}

// Lays out the current page. Paints, places the buttons, or only measures,
// depending on the walk's mode. Returns how far down the content reaches.
int layout(HDC dc, Walk::Mode mode) {
    RECT rc;
    GetClientRect(g_wnd, &rc);
    const int scroll = mode == Walk::Measure ? 0 : g_scroll;
    Walk w{dc, mode, S(kPad), rc.right - S(kPad), -scroll};
    const std::wstring addr = staffAddressW();
    const bool attention = needsAttention(g_checks);
    const bool placing = mode == Walk::Place;
    g_bodyBottom = rc.bottom - footerHeight();

    switch (g_page) {
        case PageWelcome:
            if (placing) hideRowButtons();
            header(w, 1, L"Welcome to Dental Comms",
                   L"Quick messages between the rooms in your office. Setup takes about "
                   L"30 seconds, and there's nothing to type.");
            card(w, 0, [&](Walk& c) {
                feature(c, Icon::Send, L"Message any room", L"Tap a room, then tap a message.",
                        true);
                feature(c, Icon::Bell, L"Nobody misses it",
                        L"A chime, and the message stays on their screen until someone taps "
                        L"Got it.",
                        true);
                feature(c, Icon::Globe, L"Nothing to install anywhere else",
                        L"Other computers just open a web page.", false);
            });
            w.y += S(t::space4);
            banner(w, BannerKind::Info,
                   L"Windows may ask if Dental Comms can use your network. Click Allow.");
            w.y += S(t::space4);
            w.text(g_body, t::inkMuted,
                   L"For everyday messages only. Keep your usual way of calling for help "
                   L"for anything urgent.");
            w.y += S(t::space5);
            break;

        case PageChecking: {
            const bool done = g_revealed >= g_checks.size();
            header(w, 2, L"Is this computer ready?",
                   !done       ? L"Checking a few things. This only takes a moment."
                   : attention ? L"Some things need you. Fix them here, or carry on and come "
                                 L"back to them: they stay in this window."
                               : L"Yes. Everything Dental Comms needs is in place.");
            if (anyRows(g_revealed, false))
                card(w, t::space1, [&](Walk& c) { rows(c, g_revealed, false); });
            else if (placing)
                hideRowButtons();
            w.y += S(t::space5);
            break;
        }

        case PageReady:
            if (!addr.empty()) {
                if (placing) hideRowButtons();
                header(w, 3, L"How do staff computers connect?",
                       L"On each staff computer, open a web browser and go to one of these "
                       L"addresses.");
                addressPanel(w);
                w.y += S(t::space5);
                actionRow(w, true);
                w.y += S(t::space4);
                w.text(g_body, t::inkMuted,
                       L"Tablets and phones can scan the QR code on the setup card. You can "
                       L"close this window: Dental Comms keeps running by the clock.");
                w.y += S(t::space5);
            } else {
                header(w, 3, L"Can staff computers connect?",
                       L"Not yet. Fix the items below, and the address will appear here as "
                       L"soon as they can.");
                if (anyRows(g_checks.size(), true))
                    card(w, t::space1, [&](Walk& c) { rows(c, g_checks.size(), true); });
                else if (placing)
                    hideRowButtons();
                w.y += S(t::space5);
            }
            break;

        case PageStatus: {
            std::wstring sub = L"Version " + widen(g_facts.version);
            if (!g_facts.officeName.empty())
                sub = widen(g_facts.officeName) + L"  ·  " + sub;
            const bool office = g_facts.mode == NetMode::Office;
            const Status st =
                attention ? Status{L"Needs you", Icon::Alert, t::warningTint, t::warningInk,
                                   t::warningInk}
                : office  ? Status{L"Running", Icon::Check, t::successTint, t::success, t::ink}
                          : Status{L"This computer only", Icon::Info, t::primaryTint,
                                   t::primaryInk, t::primaryInk};
            header(w, 0, L"Dental Comms", sub, &st, &g_labelPlain);
            if (!addr.empty()) {
                kicker(w, L"STAFF COMPUTERS GO TO");
                addressPanel(w);
            } else {
                banner(w, BannerKind::Warning,
                       L"Staff computers can't connect right now. See below.");
            }
            w.y += S(t::space4);
            actionRow(w, !addr.empty());
            w.y += S(t::space5);
            kicker(w, L"STATUS");
            card(w, t::space1, [&](Walk& c) { rows(c, g_checks.size(), false); });
            w.y += S(t::space5);
            break;
        }
    }

    const int content = w.y + scroll;
    const int fh = footerHeight();
    const int ftop = rc.bottom - fh;
    if (mode == Walk::Paint) {
        // The action bar: cream, with shadow-bar rising from it.
        Gfx gx(dc);
        const int n = S(12);
        for (int i = 0; i < n; ++i) {
            const BYTE a = (BYTE)(16 * (n - i) * (n - i) / (n * n));
            Gdiplus::SolidBrush b(gc(t::ink, a));
            gx.g.FillRectangle(&b, 0, ftop - i - 1, rc.right, 1);
        }
        fillRect(dc, RECT{0, ftop, rc.right, rc.bottom}, t::surfaceRaised);
    }
    if (placing) {
        const int y = ftop + S(t::space4);
        const int right = rc.right - S(kPad);
        if (bigFooter()) {
            place(g_btnPrimary, S(kPad), y, right - S(kPad), S(t::targetBig), t::surfaceRaised);
        } else {
            const int bw = buttonWidth(g_btnHide);
            place(g_btnHide, right - bw, y, bw, S(t::targetMin), t::surfaceRaised);
        }
        if (addr.empty()) ShowWindow(g_btnCopy, SW_HIDE);
        if (ipAddressW().empty()) ShowWindow(g_btnCopyIp, SW_HIDE);
    }
    return content;
}

void relayout() {
    if (!g_wnd) return;
    HDC dc = GetDC(g_wnd);
    layout(dc, Walk::Place);
    ReleaseDC(g_wnd, dc);
    InvalidateRect(g_wnd, nullptr, FALSE);
}

// Grows or shrinks the window to fit the page, so nothing is ever cut off and
// nobody has to scroll a checklist.
void fitWindow() {
    HDC dc = GetDC(g_wnd);
    const int content = layout(dc, Walk::Measure);
    ReleaseDC(g_wnd, dc);
    int want = content + footerHeight();
    if (want < S(kMinHeight)) want = S(kMinHeight);

    RECT rc;
    GetClientRect(g_wnd, &rc);
    if (abs(rc.bottom - want) <= S(2) && rc.right == S(kWidth)) return;

    const DWORD style = (DWORD)GetWindowLongPtrW(g_wnd, GWL_STYLE);
    RECT r{0, 0, S(kWidth), want};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, g_dpi);
    RECT cur;
    GetWindowRect(g_wnd, &cur);
    HMONITOR mon = MonitorFromWindow(g_wnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    int h = r.bottom - r.top;
    const int room = mi.rcWork.bottom - mi.rcWork.top;
    if (h > room) h = room;
    int top = cur.top;
    if (top + h > mi.rcWork.bottom) top = mi.rcWork.bottom - h;
    if (top < mi.rcWork.top) top = mi.rcWork.top;
    SetWindowPos(g_wnd, nullptr, cur.left, top, r.right - r.left, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

// When the screen is too short for the page, the page scrolls above a fixed
// action bar. Otherwise there is no scroll bar at all.
void updateScroll() {
    HDC dc = GetDC(g_wnd);
    const int content = layout(dc, Walk::Measure);
    ReleaseDC(g_wnd, dc);
    RECT rc;
    GetClientRect(g_wnd, &rc);
    const int view = std::max(1, (int)rc.bottom - footerHeight());
    const int maxScroll = std::max(0, content - view);
    g_scroll = std::min(std::max(g_scroll, 0), maxScroll);
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMin = 0;
    si.nMax = maxScroll ? content - 1 : 0;
    si.nPage = maxScroll ? (UINT)view : 0;
    si.nPos = g_scroll;
    SetScrollInfo(g_wnd, SB_VERT, &si, TRUE);
}

void scrollTo(int pos) {
    g_scroll = pos;
    updateScroll();
    relayout();
}

std::wstring fixButtonText(const Check& c) {
    if (g_fixRunning[(int)c.fix]) return L"Waiting for Windows…";
    return widen(c.fixLabel);
}

// Keeps one button per row that has a fix. Rebuilt only when the rows'
// fixes change, so a refresh every few seconds never makes them flicker.
void syncRowButtons() {
    std::string sig;
    for (const auto& c : g_checks)
        sig += std::to_string((int)c.fix) + ":" + c.fixLabel + "|";
    if (sig != g_rowSignature) {
        for (HWND b : g_rowButtons)
            if (b) DestroyWindow(b);
        g_rowButtons.assign(g_checks.size(), nullptr);
        for (size_t i = 0; i < g_checks.size(); ++i)
            if (g_checks[i].fix != Fix::None && !g_checks[i].fixLabel.empty())
                g_rowButtons[i] = makeButton(ID_FIX_BASE + (int)i, L"", Look::Secondary);
        g_rowSignature = sig;
    }
    for (size_t i = 0; i < g_checks.size(); ++i) {
        if (!g_rowButtons[i]) continue;
        setText(g_rowButtons[i], fixButtonText(g_checks[i]));
        EnableWindow(g_rowButtons[i], !g_fixRunning[(int)g_checks[i].fix]);
    }
}

void show(HWND b, bool on) { ShowWindow(b, on ? SW_SHOW : SW_HIDE); }

// Which buttons belong on the current page, and how they look there.
void applyPage() {
    const bool haveAddr = !staffAddress(g_facts).empty();
    show(g_btnPrimary, g_page != PageStatus);
    show(g_btnHide, g_page == PageStatus);
    const bool actions = g_page == PageStatus || (g_page == PageReady && haveAddr);
    show(g_btnCard, actions && haveAddr);
    show(g_btnOpen, actions);
    show(g_btnCopy, actions && haveAddr);
    show(g_btnCopyIp, actions && !ipAddressW().empty());
    // On the Ready page these are the useful second choices beside the main
    // action; on the status page nothing is the one main step, so all quiet.
    for (HWND b : {g_btnCard, g_btnOpen}) {
        auto it = g_btn.find(b);
        const Look want = g_page == PageReady ? Look::Secondary : Look::Quiet;
        if (it != g_btn.end() && it->second.look != want) {
            it->second.look = want;
            InvalidateRect(b, nullptr, FALSE);
        }
    }

    switch (g_page) {
        case PageWelcome:
            setText(g_btnPrimary, L"Get started");
            EnableWindow(g_btnPrimary, TRUE);
            break;
        case PageChecking: {
            // A disabled button says why it is disabled.
            const bool done = g_revealed >= g_checks.size();
            setText(g_btnPrimary, done ? L"Next" : L"Checking…");
            EnableWindow(g_btnPrimary, done);
            break;
        }
        case PageReady:
            setText(g_btnPrimary, L"Finish setup");
            EnableWindow(g_btnPrimary, TRUE);
            break;
        case PageStatus: break;
    }
    fitWindow();
    updateScroll();
    relayout();
}

// --------------------------------------------------------------- refresh
void rebuild() {
    g_checks = buildChecks(g_facts);
    syncRowButtons();
}

void refreshNow(bool deep) {
    if (!g_hooks.refresh) return;
    const long long now = GetTickCount64();
    if (!deep && IsWindowVisible(g_wnd) && now - g_lastDeep > kDeepEveryMs) deep = true;
    if (deep) g_lastDeep = now;
    g_facts = g_hooks.refresh(deep);
    rebuild();

    // Something went wrong while nobody was looking - Windows moved to a public
    // network, say. Say so once, from the clock, rather than leaving the office
    // to find out from a message that never arrived.
    const bool attention = needsAttention(g_checks);
    if (attention && !g_hadAttention && !IsWindowVisible(g_wnd)) {
        std::wstring first;
        for (const auto& c : g_checks)
            if (c.state == CheckState::NeedsYou) { first = widen(c.text); break; }
        notify(L"Dental Comms needs you", first + L". Click here to see what to do.");
    }
    g_hadAttention = attention;
    updateTray();

    applyPage();
}

void startChecking() {
    g_page = PageChecking;
    refreshNow(true);
    // The rows appear one at a time. It is quick either way, but watching each
    // one turn green is what tells a nervous first-timer that something was
    // actually checked.
    g_revealed = 0;
    SetTimer(g_wnd, TIMER_REVEAL, kRevealMs, nullptr);
    applyPage();
}

// ----------------------------------------------------------------- fixes
const wchar_t* elevatedArgs(Fix f) {
    switch (f) {
        case Fix::Firewall: return L"--fix-firewall";
        case Fix::TrustNetwork: return L"--trust-network";
        default: return nullptr;
    }
}

// Starts this exe again with administrator rights for one job. Returns 0 when
// it worked, 1 when it ran and failed, 2 when the UAC prompt was declined.
int runElevated(HWND owner, const wchar_t* args) {
    const std::wstring exe = widen(exePath());
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return GetLastError() == ERROR_CANCELLED ? 2 : 1;
    if (!sei.hProcess) return 1;
    DWORD code = 1;
    if (WaitForSingleObject(sei.hProcess, 60000) == WAIT_OBJECT_0)
        GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0 ? 0 : 1;
}

void runFix(Fix f) {
    const int slot = (int)f;
    g_fixNote[slot].clear();
    if (f == Fix::StartWithWindows) {
        std::string err;
        if (!setRunAtSignIn(true, err))
            g_fixNote[slot] =
                "Windows wouldn't let Dental Comms start by itself. It still works; "
                "just open it after restarting this computer.";
        refreshNow(false);
        return;
    }
    const wchar_t* args = elevatedArgs(f);
    if (!args || g_fixRunning[slot]) return;
    g_fixRunning[slot] = true;
    syncRowButtons();
    relayout();
    HWND owner = g_wnd;
    // Off the UI thread: the UAC prompt can sit there for as long as somebody
    // goes to find the person with the password, and the window must keep
    // painting meanwhile.
    std::thread([owner, f, args] {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const int rc = runElevated(owner, args);
        if (SUCCEEDED(co)) CoUninitialize();
        PostMessageW(owner, WM_FIXDONE, (WPARAM)f, (LPARAM)rc);
    }).detach();
}

void fixDone(Fix f, int rc) {
    const int slot = (int)f;
    g_fixRunning[slot] = false;
    if (rc == 2)
        g_fixNote[slot] =
            "That needs a computer administrator's OK. Ask whoever looks after "
            "your computers to click Yes here.";
    else if (rc != 0)
        g_fixNote[slot] =
            "Windows didn't make the change. Ask whoever looks after your "
            "computers to help with this one.";
    refreshNow(true);
}

// ------------------------------------------------------------- show/hide
void hideToTray() {
    addTray();
    ShowWindow(g_wnd, SW_HIDE);
}

void showWindow() {
    const bool wasVisible = IsWindowVisible(g_wnd) != FALSE;
    ShowWindow(g_wnd, SW_SHOW);
    ShowWindow(g_wnd, SW_RESTORE);
    SetForegroundWindow(g_wnd);
    if (!wasVisible) refreshNow(true);
}

void trayMenu() {
    const bool haveIp = !ipAddressW().empty();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_TRAY_SHOW, L"Open Dental Comms");
    AppendMenuW(m, MF_STRING | (staffAddress(g_facts).empty() ? MF_GRAYED : 0),
                IDM_TRAY_COPY, L"Copy the staff address");
    if (haveIp) AppendMenuW(m, MF_STRING, IDM_TRAY_COPY_IP, L"Copy the IP address");
    AppendMenuW(m, MF_STRING, IDM_TRAY_OPEN, L"Open in a browser");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (runAtSignInEnabled() ? MF_CHECKED : 0),
                IDM_TRAY_STARTUP, L"Start with Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_TRAY_EXIT, L"Turn Dental Comms off");

    POINT pt;
    GetCursorPos(&pt);
    // Required, or the menu will not close when the user clicks elsewhere.
    SetForegroundWindow(g_wnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, nullptr);
    PostMessage(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

void resetCopyButtons() {
    for (HWND b : {g_btnCopy, g_btnCopyIp}) {
        setIcon(b, Icon::Copy);
        setText(b, L"Copy address");
    }
}

void onCopy(HWND btn, const std::wstring& text) {
    copyText(text);
    // Only one says "Copied" at a time.
    resetCopyButtons();
    setIcon(btn, Icon::Check);
    setText(btn, L"Copied");
    SetTimer(g_wnd, TIMER_COPIED, 1500, nullptr);
}

// ----------------------------------------------------------------- window
void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    // Drawn off-screen and copied in one go, so a refresh every few seconds
    // never flickers.
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    fillRect(mem, rc, t::surface);
    SetBkMode(mem, TRANSPARENT);
    layout(mem, Walk::Paint);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// On Windows 11 the title bar takes the page's oat colour, so the window
// reads as one calm piece. Older Windows ignores these.
void styleTitleBar(HWND hwnd) {
    const DWORD kCornerPreference = 33, kBorderColor = 34, kCaptionColor = 35,
                kTextColor = 36;
    const DWORD round = 2;   // DWMWCP_ROUND
    const COLORREF caption = t::surface, text = t::ink, border = t::line;
    DwmSetWindowAttribute(hwnd, kCornerPreference, &round, sizeof(round));
    DwmSetWindowAttribute(hwnd, kBorderColor, &border, sizeof(border));
    DwmSetWindowAttribute(hwnd, kCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, kTextColor, &text, sizeof(text));
}

bool isOurButton(HWND h) { return h && g_btn.count(h) != 0; }

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_showMsg && msg == g_showMsg) {
        showWindow();
        return 0;
    }
    switch (msg) {
        case WM_CREATE: {
            g_wnd = hwnd;
            g_dpi = (int)GetDpiForWindow(hwnd);
            if (g_dpi < 72 || g_dpi > 480) g_dpi = 96;
            makeFonts();
            styleTitleBar(hwnd);
            g_btnPrimary = makeButton(ID_PRIMARY, L"Next", Look::Primary, true);
            g_btnCopy = makeButton(ID_COPY, L"Copy address", Look::Secondary);
            g_btnCopyIp = makeButton(ID_COPY_IP, L"Copy address", Look::Secondary);
            g_btnCard = makeButton(ID_CARD, L"Print setup card", Look::Quiet);
            g_btnOpen = makeButton(ID_OPEN, L"Open Dental Comms", Look::Quiet);
            g_btnHide = makeButton(ID_HIDE, L"Hide window", Look::Quiet);
            setIcon(g_btnCopy, Icon::Copy);
            setIcon(g_btnCopyIp, Icon::Copy);
            setIcon(g_btnCard, Icon::Printer);
            setIcon(g_btnOpen, Icon::External);
            for (HWND b : {g_btnCopy, g_btnCopyIp}) g_btn[b].minWidth = buttonWidth(b);
            return 0;
        }

        case WM_DPICHANGED: {
            g_dpi = HIWORD(wp);
            makeFonts();
            for (HWND b : {g_btnCopy, g_btnCopyIp}) {
                g_btn[b].minWidth = 0;
                g_btn[b].minWidth = buttonWidth(b);
            }
            const RECT* r = (const RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left,
                         r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            fitWindow();
            relayout();
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            return 0;
        }

        case WM_SIZE:
            updateScroll();
            relayout();
            return 0;

        case WM_MOUSEWHEEL:
            scrollTo(g_scroll - GET_WHEEL_DELTA_WPARAM(wp) * S(60) / WHEEL_DELTA);
            return 0;

        case WM_VSCROLL: {
            SCROLLINFO si{sizeof(si), SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &si);
            int pos = g_scroll;
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= S(40); break;
                case SB_LINEDOWN: pos += S(40); break;
                case SB_PAGEUP: pos -= (int)si.nPage; break;
                case SB_PAGEDOWN: pos += (int)si.nPage; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pos = si.nTrackPos; break;
                case SB_TOP: pos = 0; break;
                case SB_BOTTOM: pos = si.nMax; break;
            }
            scrollTo(pos);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            paint(hwnd);
            return 0;

        case WM_DRAWITEM:
            drawButton((const DRAWITEMSTRUCT*)lp);
            return TRUE;

        case WM_TIMER:
            switch (wp) {
                case TIMER_REFRESH:
                    refreshNow(false);
                    return 0;
                case TIMER_REVEAL:
                    if (g_revealed < g_checks.size()) ++g_revealed;
                    if (g_revealed >= g_checks.size()) KillTimer(hwnd, TIMER_REVEAL);
                    applyPage();
                    return 0;
                case TIMER_COPIED:
                    KillTimer(hwnd, TIMER_COPIED);
                    resetCopyButtons();
                    return 0;
            }
            return 0;

        case WM_FIXDONE:
            fixDone((Fix)wp, (int)lp);
            return 0;

        case WM_COMMAND: {
            // Drawn buttons report a quick second click as a double-click.
            // Only plain clicks count, or a double-click on Next would also
            // press the button that replaces it.
            if (lp && HIWORD(wp) != BN_CLICKED) return 0;
            int id = LOWORD(wp);
            // Enter: press the focused button, otherwise the main one.
            if (id == IDOK) {
                HWND f = GetFocus();
                if (isOurButton(f) && f != g_btnPrimary) {
                    SendMessageW(f, BM_CLICK, 0, 0);
                    return 0;
                }
                if (!IsWindowVisible(g_btnPrimary) || !IsWindowEnabled(g_btnPrimary))
                    return 0;
                id = ID_PRIMARY;
            }
            if (id >= ID_FIX_BASE && id < ID_FIX_BASE + (int)g_checks.size()) {
                runFix(g_checks[id - ID_FIX_BASE].fix);
                return 0;
            }
            switch (id) {
                case ID_PRIMARY:
                    g_scroll = 0;
                    if (g_page == PageWelcome) {
                        startChecking();
                    } else if (g_page == PageChecking) {
                        g_page = PageReady;
                        applyPage();
                    } else if (g_page == PageReady) {
                        if (g_hooks.markWelcomed) g_hooks.markWelcomed();
                        g_page = PageStatus;
                        applyPage();
                        hideToTray();
                        notify(L"Dental Comms is still running",
                               L"Click this icon any time to see your address.");
                    }
                    return 0;
                case ID_COPY:
                case IDM_TRAY_COPY:
                    onCopy(g_btnCopy, staffAddressW());
                    return 0;
                case ID_COPY_IP:
                case IDM_TRAY_COPY_IP:
                    onCopy(g_btnCopyIp, ipAddressW());
                    return 0;
                case ID_CARD:
                    openUrl(localAddressW() + L"/card");
                    return 0;
                case ID_OPEN:
                case IDM_TRAY_OPEN:
                    openUrl(localAddressW());
                    return 0;
                case ID_HIDE:
                    hideToTray();
                    return 0;
                case IDM_TRAY_SHOW:
                    showWindow();
                    return 0;
                case IDM_TRAY_STARTUP: {
                    std::string err;
                    if (!setRunAtSignIn(!runAtSignInEnabled(), err))
                        MessageBoxW(hwnd, widen(err).c_str(), L"Dental Comms",
                                    MB_ICONWARNING | MB_OK);
                    refreshNow(false);
                    return 0;
                }
                case IDM_TRAY_EXIT:
                    g_reallyQuitting = true;
                    DestroyWindow(hwnd);
                    return 0;
            }
            return 0;
        }

        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_LBUTTONUP:
                case WM_LBUTTONDBLCLK:
                case NIN_SELECT:
                case NIN_KEYSELECT:
                case NIN_BALLOONUSERCLICK:
                    showWindow();
                    break;
                case WM_RBUTTONUP:
                    g_menuFromRButton = true;
                    trayMenu();
                    break;
                case WM_CONTEXTMENU:
                    // Version 3 icons send this after WM_RBUTTONUP as well; one
                    // right-click should open one menu.
                    if (g_menuFromRButton) g_menuFromRButton = false;
                    else trayMenu();
                    break;
            }
            return 0;

        case WM_SYSCOMMAND:
            // Minimising goes to the tray rather than the taskbar: this is a
            // program that should be out of the way, not one more window to
            // wade through.
            if ((wp & 0xFFF0) == SC_MINIMIZE) {
                hideToTray();
                return 0;
            }
            break;

        case WM_CLOSE:
            // The old console window turned Dental Comms off when it was
            // closed, and people close windows. Closing now hides it instead,
            // and the tray menu is where turning it off lives.
            if (!g_reallyQuitting) {
                hideToTray();
                return 0;
            }
            break;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_REFRESH);
            removeTray();
            if (g_hooks.onQuit) g_hooks.onQuit();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ------------------------------------------------------------ one at a time
bool claimSingleInstance() {
    // Kept open for the life of the process; Windows closes it on exit.
    static HANDLE mutex = nullptr;
    mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    return mutex && GetLastError() != ERROR_ALREADY_EXISTS;
}

bool signalRunningCopy(InstanceRequest r) {
    // The first copy may still be starting up and not have its window yet.
    for (int tries = 0; tries < 20; ++tries) {
        if (HWND w = FindWindowW(kClassName, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            // This copy was started by a person, so it may bring a window to
            // the front; pass that right on to the copy that owns the window.
            AllowSetForegroundWindow(pid);
            if (r == InstanceRequest::Show)
                PostMessageW(w, RegisterWindowMessageW(kShowMessage), 0, 0);
            else
                PostMessageW(w, WM_COMMAND, IDM_TRAY_EXIT, 0);
            return true;
        }
        Sleep(250);
    }
    return false;
}

bool stopOtherServers() {
    bool any = false;
    for (const wchar_t* cls : {kClassName, kOldClassName}) {
        HWND w = nullptr;
        while ((w = FindWindowExW(nullptr, w, cls, nullptr)) != nullptr) {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            if (pid == GetCurrentProcessId()) continue;
            PostMessageW(w, WM_COMMAND, IDM_TRAY_EXIT, 0);
            any = true;
        }
    }
    return any;
}

int askChoice(const std::wstring& instruction, const std::wstring& content,
              const std::vector<Choice>& choices) {
    // Looked up rather than linked: TaskDialogIndirect only exists in the
    // version-6 common controls the manifest asks for, and a missing import
    // would stop the whole program from starting rather than just this box.
    using TaskDialogFn = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    HMODULE cc = LoadLibraryW(L"comctl32.dll");
    auto fn = cc ? (TaskDialogFn)GetProcAddress(cc, "TaskDialogIndirect") : nullptr;
    if (fn) {
        std::vector<TASKDIALOG_BUTTON> buttons;
        for (const auto& c : choices) buttons.push_back({c.id, c.text.c_str()});
        TASKDIALOGCONFIG tc{};
        tc.cbSize = sizeof(tc);
        tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
        tc.pszWindowTitle = L"Dental Comms";
        tc.pszMainIcon = TD_WARNING_ICON;
        tc.pszMainInstruction = instruction.c_str();
        tc.pszContent = content.c_str();
        tc.pButtons = buttons.data();
        tc.cButtons = (UINT)buttons.size();
        tc.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        int pressed = 0;
        if (SUCCEEDED(fn(&tc, &pressed, nullptr, nullptr))) {
            for (const auto& c : choices)
                if (c.id == pressed) return pressed;
            return 0;
        }
    }
    // No task dialog: Retry is the first choice, Cancel is close.
    const std::wstring text = instruction + L"\n\n" + content;
    const int r = MessageBoxW(nullptr, text.c_str(), L"Dental Comms",
                              MB_ICONWARNING | MB_RETRYCANCEL);
    return (r == IDRETRY && !choices.empty()) ? choices.front().id : 0;
}

// ------------------------------------------------------------ run at sign-in
namespace {
const wchar_t kRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring quotedExePath() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return L"";
    // --tray, so a computer that starts by itself comes up out of the way
    // rather than with a window across whatever is on screen.
    return L"\"" + std::wstring(path) + L"\" --tray";
}
}  // namespace

bool runAtSignInEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) !=
        ERROR_SUCCESS)
        return false;
    wchar_t buf[1024];
    DWORD size = sizeof(buf), type = 0;
    const bool found =
        RegQueryValueExW(key, kRunValue, nullptr, &type, (LPBYTE)buf, &size) ==
            ERROR_SUCCESS &&
        type == REG_SZ;
    RegCloseKey(key);
    return found;
}

bool setRunAtSignIn(bool on, std::string& error) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        error = "Windows would not let Dental Comms change the startup setting.";
        return false;
    }
    LONG rc;
    if (on) {
        const std::wstring cmd = quotedExePath();
        rc = RegSetValueExW(key, kRunValue, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                            (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kRunValue);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        error = "Windows would not let Dental Comms change the startup "
                "setting: " + lastErrorMessage((DWORD)rc);
        return false;
    }
    return true;
}

void migrateRunAtSignIn() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0,
                      KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    wchar_t buf[1024];
    DWORD size = sizeof(buf) - sizeof(wchar_t), type = 0;
    if (RegQueryValueExW(key, kOldRunValue, nullptr, &type, (LPBYTE)buf,
                         &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        buf[size / sizeof(wchar_t)] = L'\0';
        // Only an entry that started the copy in this folder is ours to
        // move: someone upgraded this copy and had it starting at sign-in.
        // The trailing slash stops "...\Office" matching "...\Office2".
        if (icontains(narrow(buf), exeDir() + "\\")) {
            const std::wstring cmd = quotedExePath();
            if (RegSetValueExW(key, kRunValue, 0, REG_SZ,
                               (const BYTE*)cmd.c_str(),
                               (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) ==
                ERROR_SUCCESS)
                RegDeleteValueW(key, kOldRunValue);
        }
    }
    RegCloseKey(key);
}

void showProblem(const std::string& text) {
    MessageBoxW(nullptr, widen(text).c_str(), L"Dental Comms",
                MB_ICONWARNING | MB_OK);
}

int runGui(HINSTANCE inst, const Facts& initial, bool showWelcome, bool startHidden,
           const GuiHooks& hooks) {
    g_inst = inst;
    g_hooks = hooks;
    g_facts = initial;
    g_checks = buildChecks(g_facts);
    g_page = showWelcome ? PageWelcome : PageStatus;
    g_showMsg = RegisterWindowMessageW(kShowMessage);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdiplus, &gsi, nullptr);

    g_icon = appIcon(GetSystemMetrics(SM_CXICON));
    g_iconSm = appIcon(GetSystemMetrics(SM_CXSMICON));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // every pixel is painted by paint()
    wc.hIcon = g_icon;
    wc.hIconSm = g_iconSm;
    RegisterClassExW(&wc);

    // A fixed width and a height that follows the page: nothing to resize.
    const DWORD style =
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    const int sysDpi = (int)GetDpiForSystem();
    const int w = MulDiv(kWidth + 16, sysDpi, 96), h = MulDiv(560, sysDpi, 96);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - w) / 2;
    const int y = work.top + ((work.bottom - work.top) - h) / 4;

    g_wnd = CreateWindowExW(0, kClassName, L"Dental Comms", style, x, y, w, h,
                            nullptr, nullptr, inst, nullptr);
    if (!g_wnd) return 1;

    rebuild();
    applyPage();

    if (startHidden) {
        addTray();
    } else {
        ShowWindow(g_wnd, SW_SHOW);
        UpdateWindow(g_wnd);
    }
    SetTimer(g_wnd, TIMER_REFRESH, kRefreshMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    deleteFonts();
    if (g_icon) DestroyIcon(g_icon);
    if (g_iconSm) DestroyIcon(g_iconSm);
    if (g_gdiplus) Gdiplus::GdiplusShutdown(g_gdiplus);
    return 0;
}

}  // namespace on
