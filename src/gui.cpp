#include "gui.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <string>
#include <thread>
#include <vector>

#include "util.h"

namespace on {
namespace {

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
    IDM_TRAY_SHOW = 200,
    IDM_TRAY_COPY,
    IDM_TRAY_OPEN,
    IDM_TRAY_STARTUP,
    IDM_TRAY_EXIT,      // 204
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

// Sizes in 96-DPI pixels; S() scales them for the monitor.
constexpr int kWidth = 620;
constexpr int kMinHeight = 400;
constexpr int kMargin = 26;
constexpr int kBtnH = 32;
constexpr int kIcon = 20;
constexpr int kIndent = 32;

const COLORREF kInk = RGB(29, 39, 51);
const COLORREF kMuted = RGB(96, 112, 130);
const COLORREF kBlue = RGB(28, 126, 214);
const COLORREF kBlueDark = RGB(16, 88, 160);
const COLORREF kBlueBg = RGB(234, 243, 252);
const COLORREF kGreen = RGB(47, 158, 68);
const COLORREF kAmber = RGB(214, 120, 8);
const COLORREF kAmberText = RGB(166, 86, 0);
const COLORREF kGrey = RGB(150, 160, 172);

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

HWND g_btnPrimary = nullptr, g_btnCopy = nullptr, g_btnCard = nullptr,
     g_btnOpen = nullptr, g_btnHide = nullptr;
std::vector<HWND> g_rowButtons;   // one per row; nullptr when it has no fix
std::string g_rowSignature;
std::string g_fixNote[4];
bool g_fixRunning[4] = {false, false, false, false};

NOTIFYICONDATAW g_tray{};
bool g_trayAdded = false;
bool g_menuFromRButton = false;
HICON g_icon = nullptr, g_iconSm = nullptr;
HFONT g_fontTitle = nullptr, g_fontBody = nullptr, g_fontBold = nullptr,
      g_fontSmall = nullptr, g_fontAddr = nullptr;
bool g_reallyQuitting = false;

int g_dpi = 96;
int S(int px) { return MulDiv(px, g_dpi, 96); }

// ------------------------------------------------------------------- icon
// Drawn rather than shipped as a .ico, for the same reason the rest of this
// program has no binary assets: nothing to lose when the folder is moved, and
// identical on every machine. A blue rounded square with a white speech
// bubble - Dental Comms is a message board, so it should look like one.
HICON makeIcon(int size) {
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP colour =
        CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!colour) {
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return nullptr;
    }
    HGDIOBJ oldBmp = SelectObject(dc, colour);

    // Transparent to start with; every pixel we paint gets its alpha set.
    memset(bits, 0, (size_t)size * size * 4);

    const int pad = size / 10;
    const int radius = size / 3;
    HBRUSH blue = CreateSolidBrush(RGB(28, 126, 214));
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = SelectObject(dc, blue);
    RoundRect(dc, pad, pad, size - pad, size - pad, radius, radius);

    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(dc, white);
    const int bx = size / 4, by = size / 4;
    const int bw = size / 2, bh = size / 3;
    RoundRect(dc, bx, by, bx + bw, by + bh, size / 8, size / 8);
    // The bubble's tail.
    POINT tail[3] = {{bx + bw / 4, by + bh - 1},
                     {bx + bw / 4 + size / 8, by + bh - 1},
                     {bx + bw / 4, by + bh + size / 6}};
    Polygon(dc, tail, 3);

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(blue);
    DeleteObject(white);

    // GDI's RoundRect leaves alpha at zero, so everything drawn would be
    // invisible in a 32-bit icon. Anything that is not still the background
    // colour is ours, and gets made opaque.
    auto* px = (BYTE*)bits;
    for (int i = 0; i < size * size; ++i) {
        BYTE b = px[i * 4 + 0], g = px[i * 4 + 1], r = px[i * 4 + 2];
        px[i * 4 + 3] = (b || g || r) ? 255 : 0;
    }

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

// ------------------------------------------------------------------ fonts
void deleteFonts() {
    for (HFONT* f : {&g_fontTitle, &g_fontBody, &g_fontBold, &g_fontSmall, &g_fontAddr})
        if (*f) { DeleteObject(*f); *f = nullptr; }
}

void makeFonts() {
    deleteFonts();
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    auto make = [&](int px, int weight) {
        LOGFONTW l = lf;
        l.lfHeight = -S(px);
        l.lfWeight = weight;
        l.lfQuality = CLEARTYPE_QUALITY;
        return CreateFontIndirectW(&l);
    };
    g_fontTitle = make(23, FW_SEMIBOLD);
    g_fontBody = make(15, FW_NORMAL);
    g_fontBold = make(15, FW_SEMIBOLD);
    g_fontSmall = make(13, FW_NORMAL);
    g_fontAddr = make(21, FW_SEMIBOLD);
}

// ----------------------------------------------------------------- facts
std::wstring staffAddressW() { return widen(staffAddress(g_facts)); }

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

// ---------------------------------------------------------------- layout
// One walk down the page serves both painting and placing the buttons, so the
// two can never disagree about where anything is.
struct Walk {
    HDC dc;
    bool paint;
    int left, right, y;

    int measure(HFONT f, const std::wstring& s, int l) const {
        RECT rc{l, 0, right, 0};
        SelectObject(dc, f);
        DrawTextW(dc, s.c_str(), -1, &rc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        return rc.bottom;
    }

    void text(HFONT f, COLORREF c, const std::wstring& s, int indent = 0,
              int after = 8) {
        const int l = left + indent;
        const int h = measure(f, s, l);
        if (paint) {
            RECT rc{l, y, right, y + h};
            SelectObject(dc, f);
            SetTextColor(dc, c);
            DrawTextW(dc, s.c_str(), -1, &rc, DT_WORDBREAK | DT_NOPREFIX);
        }
        y += h + S(after);
    }
};

int textWidth(HFONT f, const std::wstring& s) {
    HDC dc = GetDC(g_wnd);
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    SelectObject(dc, old);
    ReleaseDC(g_wnd, dc);
    return sz.cx;
}

int buttonWidth(HWND b) {
    wchar_t text[128] = {0};
    GetWindowTextW(b, text, 128);
    const int w = textWidth(g_fontBody, text) + S(36);
    return w < S(110) ? S(110) : w;
}

void roundBox(HDC dc, const RECT& r, COLORREF fill) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, S(14), S(14));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

void drawStateIcon(HDC dc, int x, int y, CheckState s) {
    const int d = S(kIcon);
    const COLORREF fill = s == CheckState::Ok         ? kGreen
                          : s == CheckState::NeedsYou ? kAmber
                          : s == CheckState::Note     ? kBlue
                                                      : kGrey;
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    Ellipse(dc, x, y, x + d, y + d);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);

    if (s == CheckState::Ok) {
        // A drawn tick, not a character: the font on a given PC may not have
        // one, and a box where the tick should be reads as a failure.
        HPEN w = CreatePen(PS_SOLID, S(2) < 2 ? 2 : S(2), RGB(255, 255, 255));
        HGDIOBJ old = SelectObject(dc, w);
        POINT pts[3] = {{x + d * 27 / 100, y + d * 52 / 100},
                        {x + d * 43 / 100, y + d * 68 / 100},
                        {x + d * 73 / 100, y + d * 34 / 100}};
        Polyline(dc, pts, 3);
        SelectObject(dc, old);
        DeleteObject(w);
        return;
    }
    const wchar_t* mark = s == CheckState::NeedsYou ? L"!"
                          : s == CheckState::Note   ? L"i"
                                                    : L"…";
    RECT r{x, y, x + d, y + d};
    SelectObject(dc, g_fontBold);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, mark, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void noteBox(Walk& w, const std::wstring& text) {
    const int pad = S(12);
    const int h = w.measure(g_fontBody, text, w.left + pad + S(kIndent)) + pad * 2;
    if (w.paint) {
        RECT box{w.left, w.y, w.right, w.y + h};
        roundBox(w.dc, box, kBlueBg);
        drawStateIcon(w.dc, w.left + pad, w.y + pad, CheckState::Note);
        RECT tr{w.left + pad + S(kIndent), w.y + pad, w.right - pad, w.y + h - pad};
        SelectObject(w.dc, g_fontBody);
        SetTextColor(w.dc, kInk);
        DrawTextW(w.dc, text.c_str(), -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
    }
    w.y += h + S(16);
}

void addressBox(Walk& w, const std::wstring& addr) {
    const int h = S(54);
    if (w.paint) {
        RECT box{w.left, w.y, w.right, w.y + h};
        roundBox(w.dc, box, kBlueBg);
        RECT tr{w.left + S(18), w.y, w.right - S(12), w.y + h};
        SelectObject(w.dc, g_fontAddr);
        SetTextColor(w.dc, kBlueDark);
        DrawTextW(w.dc, addr.c_str(), -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    w.y += h + S(12);
}

// Copy / Print a setup card / Open, side by side from the left at `y`.
void placeActionRow(int left, int y) {
    int x = left;
    for (HWND b : {g_btnCopy, g_btnCard, g_btnOpen}) {
        const int bw = buttonWidth(b);
        MoveWindow(b, x, y, bw, S(kBtnH), TRUE);
        x += bw + S(8);
    }
}

void hideRowButtons() {
    for (HWND b : g_rowButtons)
        if (b) ShowWindow(b, SW_HIDE);
}

// Lists the first `count` rows. With `skipOk`, only the ones that are not
// already green - what is left to look at.
void rows(Walk& w, size_t count, bool skipOk) {
    for (size_t i = 0; i < g_checks.size(); ++i) {
        const Check& c = g_checks[i];
        HWND btn = i < g_rowButtons.size() ? g_rowButtons[i] : nullptr;
        const bool show =
            i < count && (!skipOk || c.state != CheckState::Ok);
        if (!show) {
            if (btn && !w.paint) ShowWindow(btn, SW_HIDE);
            continue;
        }
        if (w.paint) drawStateIcon(w.dc, w.left, w.y, c.state);
        const int indent = S(kIndent);
        w.text(g_fontBold, kInk, widen(c.text), indent, c.help.empty() ? 12 : 2);
        if (!c.help.empty())
            w.text(g_fontBody, kMuted, widen(c.help), indent, btn ? 8 : 12);
        if (btn) {
            if (!w.paint) {
                MoveWindow(btn, w.left + indent, w.y, buttonWidth(btn), S(kBtnH), TRUE);
                ShowWindow(btn, SW_SHOW);
            }
            w.y += S(kBtnH) + S(8);
        }
        const std::string& note = g_fixNote[(int)c.fix];
        if (c.fix != Fix::None && !note.empty())
            w.text(g_fontSmall, kAmberText, widen(note), indent, 8);
        if (btn) w.y += S(6);
    }
}

// Lays out the current page. Paints when `paint` is set, otherwise moves the
// buttons into place. Returns how far down the content reaches.
int layout(HDC dc, bool paint) {
    RECT rc;
    GetClientRect(g_wnd, &rc);
    Walk w{dc, paint, S(kMargin), rc.right - S(kMargin), S(22)};
    const std::wstring addr = staffAddressW();
    const bool attention = needsAttention(g_checks);

    switch (g_page) {
        case PageWelcome:
            if (!paint) hideRowButtons();
            w.text(g_fontTitle, kInk, L"Welcome to Dental Comms", 0, 10);
            w.text(g_fontBody, kInk,
                   L"This computer will pass messages between the rooms in your "
                   L"office. Setup takes about 30 seconds, and there's nothing to "
                   L"type.",
                   0, 18);
            noteBox(w, L"Windows may ask if Dental Comms can use your network. "
                       L"Click Allow.");
            w.text(g_fontSmall, kMuted,
                   L"For everyday messages only. Keep your usual way of calling "
                   L"for help for anything urgent.",
                   0, 0);
            break;

        case PageChecking: {
            const bool done = g_revealed >= g_checks.size();
            w.text(g_fontTitle, kInk,
                   !done       ? L"Checking this computer…"
                   : attention ? L"Almost there"
                               : L"Everything looks good",
                   0, 4);
            w.text(g_fontBody, kMuted,
                   !done      ? L"This only takes a moment."
                   : attention ? L"Put right the items marked !, or carry on and "
                                 L"come back to them. They stay in this window."
                               : L"Here's what was checked.",
                   0, 18);
            rows(w, g_revealed, false);
            break;
        }

        case PageReady:
            if (!addr.empty()) {
                if (!paint) hideRowButtons();
                w.text(g_fontTitle, kInk, L"You're all set", 0, 10);
                w.text(g_fontBody, kInk,
                       L"On every other computer, open a web browser and go to:", 0, 10);
                addressBox(w, addr);
                if (!paint) placeActionRow(w.left, w.y);
                w.y += S(kBtnH) + S(18);
                w.text(g_fontSmall, kMuted,
                       L"Tablets and phones can scan the code on the setup card.", 0, 4);
                w.text(g_fontSmall, kMuted,
                       L"You can close this window. Dental Comms keeps running in the "
                       L"corner by the clock.",
                       0, 0);
            } else {
                w.text(g_fontTitle, kInk, L"Almost there", 0, 10);
                w.text(g_fontBody, kInk,
                       L"Other computers can't reach this one yet. Put right the "
                       L"items below, and the address to use will appear here as "
                       L"soon as they can.",
                       0, 18);
                rows(w, g_checks.size(), true);
            }
            break;

        case PageStatus: {
            w.text(g_fontTitle, kInk,
                   attention ? L"Dental Comms needs you" : L"Dental Comms is running",
                   0, 2);
            std::wstring sub = L"Version " + widen(g_facts.version);
            if (!g_facts.officeName.empty())
                sub = widen(g_facts.officeName) + L"   ·   " + sub;
            w.text(g_fontSmall, kMuted, sub, 0, 16);
            if (!addr.empty()) {
                w.text(g_fontBody, kInk, L"Staff computers use this address:", 0, 8);
                addressBox(w, addr);
            } else {
                w.text(g_fontBold, kAmberText,
                       L"Staff computers can't connect right now. See below.", 0, 14);
            }
            rows(w, g_checks.size(), false);
            break;
        }
    }

    const int content = w.y;
    if (!paint) {
        const int bottom = rc.bottom - S(kMargin) - S(kBtnH);
        const int right = rc.right - S(kMargin);
        if (IsWindowVisible(g_btnPrimary)) {
            const int bw = buttonWidth(g_btnPrimary);
            MoveWindow(g_btnPrimary, right - bw, bottom, bw, S(kBtnH), TRUE);
        }
        if (g_page == PageStatus) {
            placeActionRow(S(kMargin), bottom);
            const int bw = buttonWidth(g_btnHide);
            MoveWindow(g_btnHide, right - bw, bottom, bw, S(kBtnH), TRUE);
        }
    }
    return content;
}

void relayout() {
    if (!g_wnd) return;
    HDC dc = GetDC(g_wnd);
    layout(dc, false);
    ReleaseDC(g_wnd, dc);
    InvalidateRect(g_wnd, nullptr, FALSE);
}

// Grows or shrinks the window to fit the page, so nothing is ever cut off and
// nobody has to scroll a checklist.
void fitWindow() {
    HDC dc = GetDC(g_wnd);
    const int content = layout(dc, false);
    ReleaseDC(g_wnd, dc);
    int want = content + S(16) + S(kBtnH) + S(kMargin);
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

// --------------------------------------------------------------- buttons
// Only when it changes: setting a button's text repaints it, and doing that
// on every refresh would make the buttons flicker.
void setText(HWND b, const std::wstring& text) {
    wchar_t cur[160] = {0};
    GetWindowTextW(b, cur, 160);
    if (text != cur) SetWindowTextW(b, text.c_str());
}

HWND makeButton(int id, const wchar_t* text, bool primary = false) {
    HWND b = CreateWindowExW(
        0, L"BUTTON", text,
        WS_CHILD | WS_TABSTOP | (primary ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), 0, 0,
        10, 10, g_wnd, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    return b;
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
                g_rowButtons[i] = makeButton(ID_FIX_BASE + (int)i, L"");
        g_rowSignature = sig;
    }
    for (size_t i = 0; i < g_checks.size(); ++i) {
        if (!g_rowButtons[i]) continue;
        setText(g_rowButtons[i], fixButtonText(g_checks[i]));
        EnableWindow(g_rowButtons[i], !g_fixRunning[(int)g_checks[i].fix]);
    }
}

void show(HWND b, bool on) { ShowWindow(b, on ? SW_SHOW : SW_HIDE); }


// Which buttons belong on the current page.
void applyPage() {
    const bool haveAddr = !staffAddress(g_facts).empty();
    show(g_btnPrimary, g_page != PageStatus);
    show(g_btnHide, g_page == PageStatus);
    const bool actions = g_page == PageStatus || (g_page == PageReady && haveAddr);
    show(g_btnCopy, actions);
    show(g_btnCard, actions);
    show(g_btnOpen, actions);
    EnableWindow(g_btnCopy, haveAddr);
    setText(g_btnOpen, g_page == PageReady ? L"Open it here" : L"Open in a browser");

    switch (g_page) {
        case PageWelcome: setText(g_btnPrimary, L"Get started"); break;
        case PageChecking:
            setText(g_btnPrimary, L"Next");
            EnableWindow(g_btnPrimary, g_revealed >= g_checks.size());
            break;
        case PageReady:
            setText(g_btnPrimary, L"Done");
            EnableWindow(g_btnPrimary, TRUE);
            break;
        case PageStatus: break;
    }
    fitWindow();
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
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_TRAY_SHOW, L"Open Dental Comms");
    AppendMenuW(m, MF_STRING | (staffAddress(g_facts).empty() ? MF_GRAYED : 0),
                IDM_TRAY_COPY, L"Copy the staff address");
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

void onCopy() {
    copyText(staffAddressW());
    SetWindowTextW(g_btnCopy, L"Copied");
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
    FillRect(mem, &rc, GetSysColorBrush(COLOR_WINDOW));
    SetBkMode(mem, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(mem, g_fontBody);
    layout(mem, true);
    SelectObject(mem, oldFont);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void setAllFonts() {
    for (HWND b : {g_btnPrimary, g_btnCopy, g_btnCard, g_btnOpen, g_btnHide})
        if (b) SendMessageW(b, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    for (HWND b : g_rowButtons)
        if (b) SendMessageW(b, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
}

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
            g_btnPrimary = makeButton(ID_PRIMARY, L"Next", true);
            g_btnCopy = makeButton(ID_COPY, L"Copy address");
            g_btnCard = makeButton(ID_CARD, L"Print a setup card");
            g_btnOpen = makeButton(ID_OPEN, L"Open in a browser");
            g_btnHide = makeButton(ID_HIDE, L"Hide");
            return 0;
        }

        case WM_DPICHANGED: {
            g_dpi = HIWORD(wp);
            makeFonts();
            setAllFonts();
            const RECT* r = (const RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left,
                         r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            fitWindow();
            relayout();
            return 0;
        }

        case WM_SIZE:
            relayout();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            paint(hwnd);
            return 0;

        case WM_CTLCOLORBTN:
            // The corners around a themed button are painted in whatever this
            // returns; it has to match the page or they show as grey squares.
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

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
                    SetWindowTextW(g_btnCopy, L"Copy address");
                    return 0;
            }
            return 0;

        case WM_FIXDONE:
            fixDone((Fix)wp, (int)lp);
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id >= ID_FIX_BASE && id < ID_FIX_BASE + (int)g_checks.size()) {
                runFix(g_checks[id - ID_FIX_BASE].fix);
                return 0;
            }
            switch (id) {
                case ID_PRIMARY:
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
                    onCopy();
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

    g_icon = makeIcon(256);
    g_iconSm = makeIcon(GetSystemMetrics(SM_CXSMICON));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = g_icon;
    wc.hIconSm = g_iconSm;
    RegisterClassExW(&wc);

    // A fixed width and a height that follows the page: nothing to resize.
    const DWORD style =
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    const int sysDpi = (int)GetDpiForSystem();
    const int w = MulDiv(kWidth + 16, sysDpi, 96), h = MulDiv(520, sysDpi, 96);
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
    return 0;
}

}  // namespace on
