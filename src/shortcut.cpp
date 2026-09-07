// Builds a Windows shortcut that opens Dental Flow in its own window.
//
// This exists because of a specific failure the office kept hitting. The
// "⧉ Pop out" button in the page calls window.open(), and browsers only honour
// that inside a real click - so it can never fire by itself at sign-in. A
// staff computer that restarts comes up with Dental Flow as an ordinary tab
// behind everything else, and somebody has to notice and press a button.
//
// A Chromium browser started with --app=URL opens exactly the window the
// pop-out button was trying to make: no tabs, no address bar, its own taskbar
// entry. No pop-up blocker is involved because no pop-up is involved. Dropped
// into shell:startup, it needs nobody to press anything, ever.
//
// The shortcut is built here rather than described in a help page because
// "paste this command line into a shortcut's target field" is not an
// instruction an office should have to follow on every computer.
#include "shortcut.h"

#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>

#include "util.h"

namespace on {
namespace {

// Where these browsers install on a normal Windows machine. Edge is first
// because it is on every supported Windows and cannot be uninstalled, so a
// shortcut built against it works on a computer nobody has set up yet.
const wchar_t* kEdgePaths[] = {
    L"%ProgramFiles(x86)%\\Microsoft\\Edge\\Application\\msedge.exe",
    L"%ProgramFiles%\\Microsoft\\Edge\\Application\\msedge.exe",
};
const wchar_t* kChromePaths[] = {
    L"%ProgramFiles%\\Google\\Chrome\\Application\\chrome.exe",
    L"%ProgramFiles(x86)%\\Google\\Chrome\\Application\\chrome.exe",
    L"%LocalAppData%\\Google\\Chrome\\Application\\chrome.exe",
};

std::wstring expand(const wchar_t* raw) {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = ExpandEnvironmentStringsW(raw, buf, MAX_PATH * 2);
    if (n == 0 || n > MAX_PATH * 2) return L"";
    return buf;
}

// The first of these that is actually on this computer. Returns empty when a
// browser is not installed, which the caller reports rather than producing a
// shortcut that points at nothing.
std::wstring findBrowser(bool chrome) {
    const wchar_t* const* list = chrome ? kChromePaths : kEdgePaths;
    const size_t count = chrome ? (sizeof(kChromePaths) / sizeof(*kChromePaths))
                                : (sizeof(kEdgePaths) / sizeof(*kEdgePaths));
    for (size_t i = 0; i < count; ++i) {
        const std::wstring p = expand(list[i]);
        if (!p.empty() && PathFileExistsW(p.c_str())) return p;
    }
    return L"";
}

}  // namespace

bool buildStartupShortcut(const std::string& url, bool chrome,
                          std::string& out, std::string& error) {
    const std::wstring browser = findBrowser(chrome);
    if (browser.empty()) {
        error = chrome ? "Google Chrome is not installed on this computer."
                       : "Microsoft Edge was not found on this computer.";
        return false;
    }

    // A shortcut is a COM object that can only be written to a real path, so
    // it goes to a temp file and is read straight back. It is deleted either
    // way - what the caller wants is the bytes, not a file on the server.
    wchar_t tempDir[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempDir)) {
        error = "Windows would not give a temporary folder.";
        return false;
    }
    wchar_t tempFile[MAX_PATH];
    if (!GetTempFileNameW(tempDir, L"dfk", 0, tempFile)) {
        error = "Windows would not make a temporary file.";
        return false;
    }
    // GetTempFileName makes a .tmp; IPersistFile writes a shortcut wherever it
    // is pointed, and the extension it is saved under does not matter here
    // because the browser only ever sees the bytes.
    std::wstring lnkPath = tempFile;

    // The server may already have COM up for something else; either way this
    // has to leave the apartment as it found it.
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool weInitialised = SUCCEEDED(initHr);

    bool ok = false;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, (void**)&link))) {
        const std::wstring args = L"--app=" + widen(url);
        link->SetPath(browser.c_str());
        link->SetArguments(args.c_str());
        link->SetDescription(L"Dental Flow - opens in its own window");
        // The browser's own icon, so it looks like what it opens. Dental
        // Flow's icon lives in a web manifest, which a .lnk cannot read.
        link->SetIconLocation(browser.c_str(), 0);

        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void**)&file))) {
            if (SUCCEEDED(file->Save(lnkPath.c_str(), TRUE))) {
                std::string bytes;
                if (readFile(narrow(lnkPath), bytes) && !bytes.empty()) {
                    out.swap(bytes);
                    ok = true;
                } else {
                    error = "The shortcut was written but could not be read "
                            "back.";
                }
            } else {
                error = "Windows would not write the shortcut.";
            }
            file->Release();
        } else {
            error = "Windows would not save the shortcut.";
        }
        link->Release();
    } else {
        error = "Windows would not create a shortcut.";
    }

    DeleteFileW(lnkPath.c_str());
    if (weInitialised) CoUninitialize();
    if (!ok && error.empty()) error = "The shortcut could not be built.";
    return ok;
}

bool browserAvailable(bool chrome) { return !findBrowser(chrome).empty(); }

}  // namespace on
