// Builds the script that makes Dental Comms open by itself at sign-in.
//
// This exists because of a specific failure the office kept hitting. The
// "⧉ Pop out" button in the page calls window.open(), and browsers only honour
// that inside a real click - so it can never fire by itself at sign-in. A
// staff computer that restarts comes up with Dental Comms as an ordinary tab
// behind everything else, and somebody has to notice and press a button.
//
// A Chromium browser started with --app=URL opens exactly the window the
// pop-out button was trying to make: no tabs, no address bar, its own taskbar
// entry. No pop-up blocker is involved because no pop-up is involved. Dropped
// into shell:startup, it needs nobody to press anything, ever.
//
// This used to build that shortcut here and hand over the .lnk itself. That
// cannot work. Chromium treats .lnk as shell-integrated and rewrites the
// extension to "download" on the way to disk - see IsShellIntegratedExtension
// in net/base/filename_util_internal.cc, guarding against CVE-2010-2568 - so
// the office received a file called download.download that Windows would not
// open. No response header opts out of it, in Chrome or in Edge.
//
// So the shortcut is made on the staff computer instead, by a small batch file
// carrying this office's address. That is not merely a way around the rename;
// it is the more truthful arrangement. Which browser to point at is a question
// about the computer the window will open on, and building the shortcut here
// could only ever answer it about the server.
//
// It is a script rather than a page of instructions because "paste this
// command line into a shortcut's target field" is not something an office
// should have to do correctly on every computer.
#include "shortcut.h"

namespace on {
namespace {

// Where these browsers install on a normal Windows machine, checked on the
// computer that runs the script. Edge is listed first among its own paths
// because it is on every supported Windows and cannot be uninstalled.
struct Candidate {
    const char* path;
    const char* name;
};
const Candidate kEdge[] = {
    {"%ProgramFiles(x86)%\\Microsoft\\Edge\\Application\\msedge.exe",
     "Microsoft Edge"},
    {"%ProgramFiles%\\Microsoft\\Edge\\Application\\msedge.exe",
     "Microsoft Edge"},
};
const Candidate kChrome[] = {
    {"%ProgramFiles%\\Google\\Chrome\\Application\\chrome.exe",
     "Google Chrome"},
    {"%ProgramFiles(x86)%\\Google\\Chrome\\Application\\chrome.exe",
     "Google Chrome"},
    {"%LocalAppData%\\Google\\Chrome\\Application\\chrome.exe",
     "Google Chrome"},
};

// The address ends up inside a batch file, where & | > ^ and friends are
// commands rather than text, and it arrives in the Host header - which is
// whatever the client chose to send, not something this server picked. So only
// the shape of a real host and port is allowed through, and anything else is
// refused rather than escaped. Nothing an office can legitimately reach this
// server on is turned away: names, IPv4, bracketed IPv6 and :port all pass.
bool safeUrl(const std::string& url) {
    static const std::string kPrefix = "http://";
    if (url.compare(0, kPrefix.size(), kPrefix) != 0) return false;
    const std::string host = url.substr(kPrefix.size());
    if (host.empty() || host.size() > 255) return false;
    for (size_t i = 0; i < host.size(); ++i) {
        const char c = host[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                        c == '_' || c == ':' || c == '[' || c == ']';
        if (!ok) return false;
    }
    return true;
}

}  // namespace

bool buildStartupScript(const std::string& url, bool chrome, std::string& out,
                        std::string& error) {
    if (!safeUrl(url)) {
        error = "The address this server was reached on is not one a startup "
                "shortcut can be built for.";
        return false;
    }

    // cmd.exe wants CRLF and reads the file in the console's OEM code page, so
    // every line below ends properly and every character in it stays ASCII. A
    // stray UTF-8 dash here arrives as mojibake on a staff computer.
    std::string s;
    const auto line = [&s](const std::string& text) {
        s += text;
        s += "\r\n";
    };

    line("@echo off");
    line("setlocal");
    line("title Dental Comms - open at startup");
    line("");
    line("rem  Dental Comms - make it open by itself when you sign in.");
    line("rem");
    line("rem  The Dental Comms server built this for the address below. Run it");
    line("rem  on each computer that should open Dental Comms automatically. It");
    line("rem  looks for the browser installed on THIS computer and puts a");
    line("rem  shortcut in your Startup folder. Nothing is installed and");
    line("rem  nothing is left running - delete that shortcut to undo it.");
    line("");
    line("set \"DC_URL=" + url + "\"");
    line("set \"DC_STARTUP=%APPDATA%\\Microsoft\\Windows\\Start "
         "Menu\\Programs\\Startup\"");
    line("set \"DC_LNK=%DC_STARTUP%\\Dental Comms.lnk\"");
    line("set \"DC_BROWSER=\"");
    line("set \"DC_NAME=\"");
    line("");
    line("echo.");
    line("echo   Dental Comms");
    line("echo   Address: %DC_URL%");
    line("echo.");
    line("");
    line("rem  The browser that was asked for first, then the other one. Which");
    line("rem  browser opens the window matters far less than a window opening,");
    line("rem  so a computer with only the other one still gets set up.");

    const size_t kEdgeCount = sizeof(kEdge) / sizeof(*kEdge);
    const size_t kChromeCount = sizeof(kChrome) / sizeof(*kChrome);
    const Candidate* const first = chrome ? kChrome : kEdge;
    const size_t firstCount = chrome ? kChromeCount : kEdgeCount;
    const Candidate* const second = chrome ? kEdge : kChrome;
    const size_t secondCount = chrome ? kEdgeCount : kChromeCount;
    for (size_t i = 0; i < firstCount; ++i)
        line(std::string("call :try \"") + first[i].path + "\" \"" +
             first[i].name + "\"");
    for (size_t i = 0; i < secondCount; ++i)
        line(std::string("call :try \"") + second[i].path + "\" \"" +
             second[i].name + "\"");

    line("");
    line("if not defined DC_BROWSER goto :nobrowser");
    line("if not exist \"%DC_STARTUP%\" mkdir \"%DC_STARTUP%\"");
    line("");
    line("rem  A shortcut is a COM object, so PowerShell is what makes it. The");
    line("rem  values go across in the environment rather than on the command");
    line("rem  line, so that a space in a path cannot come apart on the way.");
    line("powershell -NoProfile -ExecutionPolicy Bypass -Command "
         "\"$ErrorActionPreference='Stop'; "
         "$s=(New-Object -ComObject WScript.Shell).CreateShortcut($env:DC_LNK); "
         "$s.TargetPath=$env:DC_BROWSER; "
         "$s.Arguments='--app=' + $env:DC_URL; "
         "$s.IconLocation=$env:DC_BROWSER + ',0'; "
         "$s.Description='Dental Comms - opens in its own window'; "
         "$s.Save()\"");
    line("");
    line("if errorlevel 1 goto :failed");
    line("if not exist \"%DC_LNK%\" goto :failed");
    line("");
    line("rem  The shortcut this made before the rename to Dental Comms. Left");
    line("rem  in place it would open a second window at every sign-in.");
    line("if exist \"%DC_STARTUP%\\Dental Flow.lnk\" "
         "del \"%DC_STARTUP%\\Dental Flow.lnk\"");
    line("");
    line("echo   Done. Dental Comms will open by itself when you next sign in.");
    line("echo.");
    line("echo   Browser:  %DC_NAME%");
    line("echo   Shortcut: %DC_LNK%");
    line("echo.");
    line("echo   Want it on the desktop or taskbar too? Copy that shortcut");
    line("echo   there, or right-click it and choose Pin to taskbar.");
    line("echo   To undo all of this, delete that shortcut.");
    line("echo.");
    line("");
    line("rem  Opening it once here is the whole check. Without it the only");
    line("rem  way to find out whether this worked is to restart the computer.");
    line("choice /c YN /n /m \"   Open Dental Comms now to check? [Y/N] \"");
    line("if errorlevel 2 goto :eof");
    line("start \"\" \"%DC_BROWSER%\" --app=%DC_URL%");
    line("goto :eof");
    line("");
    line(":try");
    line("if defined DC_BROWSER goto :eof");
    line("if exist \"%~1\" (");
    line("  set \"DC_BROWSER=%~1\"");
    line("  set \"DC_NAME=%~2\"");
    line(")");
    line("goto :eof");
    line("");
    line(":nobrowser");
    line("echo   Could not find Microsoft Edge or Google Chrome on this");
    line("echo   computer.");
    line("echo.");
    line("echo   Dental Comms needs one of them to open in its own window.");
    line("echo   Install either one, then run this again.");
    line("echo.");
    line("pause");
    line("exit /b 1");
    line("");
    line(":failed");
    line("echo   Windows would not create the shortcut.");
    line("echo.");
    line("echo   You can still do it by hand: press Windows+R, type");
    line("echo   shell:startup, and make a shortcut in the folder that opens");
    line("echo   whose target is exactly this:");
    line("echo.");
    line("echo      \"%DC_BROWSER%\" --app=%DC_URL%");
    line("echo.");
    line("pause");
    line("exit /b 1");

    out.swap(s);
    return true;
}

}  // namespace on
