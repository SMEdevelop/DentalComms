#include "gui.h"

#include <shellapi.h>
#include <windowsx.h>

#include <string>
#include <vector>

#include "util.h"

namespace on {
namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT kTrayId = 1;

enum {
    ID_COPY = 100,
    ID_OPEN,
    ID_HIDE,
    IDM_TRAY_SHOW = 200,
    IDM_TRAY_COPY,
    IDM_TRAY_OPEN,
    IDM_TRAY_STARTUP,
    IDM_TRAY_EXIT,
};

const wchar_t kClassName[] = L"DentalFlowServer";
const wchar_t kRunValue[] = L"DentalFlow";

GuiInfo g_info;
HWND g_wnd = nullptr;
HWND g_addrEdit = nullptr;
NOTIFYICONDATAW g_tray{};
bool g_trayAdded = false;
HICON g_icon = nullptr, g_iconSm = nullptr;
HFONT g_font = nullptr, g_fontBold = nullptr;
void (*g_onQuit)() = nullptr;
bool g_reallyQuitting = false;

int g_dpi = 96;
int S(int px) { return MulDiv(px, g_dpi, 96); }

// ------------------------------------------------------------------- icon
// Drawn rather than shipped as a .ico, for the same reason the rest of this
// program has no binary assets: nothing to lose when the folder is moved, and
// identical on every machine. A blue rounded square with a white speech
// bubble - Dental Flow is a message board, so it should look like one.
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

// ------------------------------------------------------------------ text
std::string bestAddress() {
    if (g_info.addresses.empty()) return "";
    char buf[256];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "http://%s:%d",
                g_info.addresses[0].c_str(), g_info.port);
    return buf;
}

std::string addressBlock() {
    std::string out;
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "On this computer:\r\n    http://localhost:%d\r\n", g_info.port);
    out += line;

    if (g_info.localOnly) {
        out +=
            "\r\nStaff computers CANNOT connect right now.\r\n"
            "Dental Flow holds back on ";
        out += g_info.networkName;
        out +=
            " networks because it has no\r\npassword on it. If this is your "
            "office network, tell Windows so:\r\n"
            "Settings > Network & internet > (your connection) > set the\r\n"
            "network profile to Private, then start Dental Flow again.\r\n";
        return out;
    }

    if (g_info.addresses.empty()) {
        out +=
            "\r\nOther computers:\r\n    (this computer has no network address "
            "to offer)\r\n";
        return out;
    }

    out += "\r\nOther computers - bookmark the first one:\r\n";
    for (const std::string& a : g_info.addresses) {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "    http://%s:%d\r\n",
                    a.c_str(), g_info.port);
        out += line;
    }
    if (!g_info.computer.empty() && g_info.addresses.size() > 1)
        out +=
            "\r\nThe name keeps working even if the router gives this\r\n"
            "computer a different number later. The numbers do not.\r\n";
    return out;
}

void copyAddress() {
    const std::string a = bestAddress();
    if (a.empty() || !OpenClipboard(g_wnd)) return;
    EmptyClipboard();
    const std::wstring w = widen(a);
    const size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* dst = GlobalLock(mem)) {
            memcpy(dst, w.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        }
    }
    CloseClipboard();
}

void openInBrowser() {
    char buf[64];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "http://localhost:%d", g_info.port);
    ShellExecuteW(nullptr, L"open", widen(buf).c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

// ------------------------------------------------------------------- tray
void addTray() {
    if (g_trayAdded) return;
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_wnd;
    g_tray.uID = kTrayId;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = WM_TRAY;
    g_tray.hIcon = g_iconSm;
    const std::string tip = g_info.localOnly
                                ? "Dental Flow - this computer only"
                                : "Dental Flow is running";
    wcsncpy_s(g_tray.szTip, widen(tip).c_str(), _TRUNCATE);
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_tray) != FALSE;
}

void removeTray() {
    if (!g_trayAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &g_tray);
    g_trayAdded = false;
}

void hideToTray() {
    addTray();
    ShowWindow(g_wnd, SW_HIDE);
}

void showWindow() {
    ShowWindow(g_wnd, SW_SHOW);
    ShowWindow(g_wnd, SW_RESTORE);
    SetForegroundWindow(g_wnd);
}

void trayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_TRAY_SHOW, L"Open Dental Flow");
    AppendMenuW(m, MF_STRING, IDM_TRAY_COPY, L"Copy the staff address");
    AppendMenuW(m, MF_STRING, IDM_TRAY_OPEN, L"Open in a browser");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m,
                MF_STRING | (runAtSignInEnabled() ? MF_CHECKED : 0),
                IDM_TRAY_STARTUP, L"Start with Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_TRAY_EXIT, L"Turn Dental Flow off");

    POINT pt;
    GetCursorPos(&pt);
    // Required, or the menu will not close when the user clicks elsewhere.
    SetForegroundWindow(g_wnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, nullptr);
    PostMessage(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

// ----------------------------------------------------------------- window
void layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int pad = S(14);
    const int btnH = S(30);
    const int top = S(74);
    const int bottom = rc.bottom - pad - btnH - S(10);

    if (g_addrEdit)
        MoveWindow(g_addrEdit, pad, top, rc.right - pad * 2,
                   (bottom > top ? bottom - top : S(40)), TRUE);

    const int by = rc.bottom - pad - btnH;
    int x = pad;
    for (int id : {ID_COPY, ID_OPEN, ID_HIDE}) {
        const int w = id == ID_HIDE ? S(150) : S(140);
        if (HWND b = GetDlgItem(hwnd, id)) MoveWindow(b, x, by, w, btnH, TRUE);
        x += w + S(8);
    }
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
    SetBkMode(dc, TRANSPARENT);

    RECT line{S(14), S(14), rc.right - S(14), S(40)};
    HGDIOBJ old = SelectObject(dc, g_fontBold);
    SetTextColor(dc, RGB(20, 20, 20));
    const std::string title =
        g_info.officeName.empty()
            ? std::string("Dental Flow is running")
            : g_info.officeName + " - Dental Flow is running";
    DrawTextW(dc, widen(title).c_str(), -1, &line,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(dc, g_font);
    RECT sub{S(14), S(40), rc.right - S(14), S(66)};
    std::string status = "Version " + g_info.version + "   |   Network: " +
                         g_info.networkName;
    status += g_info.localOnly ? "   |   this computer only"
                               : "   |   staff computers can connect";
    SetTextColor(dc, g_info.localOnly ? RGB(170, 60, 20) : RGB(90, 90, 90));
    DrawTextW(dc, widen(status).c_str(), -1, &sub,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(dc, old);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_dpi = (int)GetDpiForWindow(hwnd);
            if (g_dpi < 72 || g_dpi > 480) g_dpi = 96;

            NONCLIENTMETRICSW ncm{sizeof(ncm)};
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            LOGFONTW lf = ncm.lfMessageFont;
            lf.lfHeight = -S(15);
            g_font = CreateFontIndirectW(&lf);
            lf.lfHeight = -S(19);
            lf.lfWeight = FW_SEMIBOLD;
            g_fontBold = CreateFontIndirectW(&lf);

            // A read-only multiline edit rather than a label: the addresses
            // are the one thing anyone needs out of this window, and an edit
            // lets them be selected and copied by hand as well as by button.
            g_addrEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", widen(addressBlock()).c_str(),
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                    ES_READONLY | ES_AUTOVSCROLL,
                0, 0, 10, 10, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(g_addrEdit, WM_SETFONT, (WPARAM)g_font, TRUE);

            auto button = [&](int id, const wchar_t* text) {
                HWND b = CreateWindowExW(
                    0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
            };
            button(ID_COPY, L"Copy staff address");
            button(ID_OPEN, L"Open in a browser");
            button(ID_HIDE, L"Hide in the tray");

            layout(hwnd);
            return 0;
        }

        case WM_SIZE:
            layout(hwnd);
            return 0;

        case WM_PAINT:
            paint(hwnd);
            return 0;

        case WM_CTLCOLORSTATIC:
            // The read-only edit paints itself grey by default, which reads as
            // "disabled" on the one control anybody wants to read. The text
            // colour has to be set as well: without it the control keeps
            // whatever was last selected into that DC, so the addresses came
            // out in the warning colour the status line uses.
            SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
            SetTextColor((HDC)wp, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_COPY:
                case IDM_TRAY_COPY:
                    copyAddress();
                    return 0;
                case ID_OPEN:
                case IDM_TRAY_OPEN:
                    openInBrowser();
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
                        MessageBoxW(hwnd, widen(err).c_str(), L"Dental Flow",
                                    MB_ICONWARNING | MB_OK);
                    return 0;
                }
                case IDM_TRAY_EXIT:
                    g_reallyQuitting = true;
                    DestroyWindow(hwnd);
                    return 0;
            }
            return 0;

        case WM_TRAY:
            if (LOWORD(lp) == WM_LBUTTONDBLCLK || LOWORD(lp) == WM_LBUTTONUP)
                showWindow();
            else if (LOWORD(lp) == WM_RBUTTONUP)
                trayMenu();
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
            // The old console window turned Dental Flow off when it was
            // closed, and people close windows. Closing now hides it instead,
            // and the tray menu is where turning it off lives.
            if (!g_reallyQuitting) {
                hideToTray();
                return 0;
            }
            break;

        case WM_DESTROY:
            removeTray();
            if (g_onQuit) g_onQuit();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

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
        error = "Windows would not let Dental Flow change the startup setting.";
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
        error = "Windows would not let Dental Flow change the startup "
                "setting: " + lastErrorMessage((DWORD)rc);
        return false;
    }
    return true;
}

void showProblem(const std::string& text) {
    MessageBoxW(nullptr, widen(text).c_str(), L"Dental Flow",
                MB_ICONWARNING | MB_OK);
}

int runGui(HINSTANCE inst, const GuiInfo& info, bool startHidden,
           void (*onQuit)()) {
    g_info = info;
    g_onQuit = onQuit;
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

    const int w = 560, h = 420;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - w) / 2;
    const int y = work.top + ((work.bottom - work.top) - h) / 3;

    g_wnd = CreateWindowExW(0, kClassName, L"Dental Flow",
                            WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr,
                            inst, nullptr);
    if (!g_wnd) return 1;

    if (startHidden) {
        addTray();
    } else {
        ShowWindow(g_wnd, SW_SHOW);
        UpdateWindow(g_wnd);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_font) DeleteObject(g_font);
    if (g_fontBold) DeleteObject(g_fontBold);
    if (g_icon) DestroyIcon(g_icon);
    if (g_iconSm) DestroyIcon(g_iconSm);
    return 0;
}

}  // namespace on
