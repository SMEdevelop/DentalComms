// The server's window and its notification-area icon.
//
// Dental Flow spent its first versions as a console program, which worked but
// asked the office to understand something they should not have to: that a
// black window full of text is load-bearing, and that closing it turns the
// messaging off. Staff close windows. This replaces it with an ordinary small
// window that says what the addresses are, and that tucks itself into the
// notification area instead of stopping when it is closed.
//
// Nothing here knows how to serve a request. The accept loop runs on its own
// thread and this only reports what it is doing, so the window being busy -
// dragged, or a menu open - can never stall a message.
#pragma once
#include <string>
#include <vector>
#include <windows.h>

namespace on {

// Everything the window puts on screen, worked out before it opens. Passing it
// in rather than letting the window recompute it keeps the "where can we be
// reached" logic in one place, next to the code that binds the socket.
struct GuiInfo {
    std::string version;
    int port = 0;
    std::string networkName;      // "Private", "Public", ...
    bool localOnly = false;       // staff cannot connect
    std::string computer;         // this machine's name, may be empty
    std::vector<std::string> addresses;   // best first, the one to bookmark
    std::string officeName;
};

// Runs the window and the message loop. Returns when the user quits, at which
// point the caller should stop the server. `startHidden` opens straight into
// the notification area without showing the window at all.
//
// `onQuit` is called on the UI thread just before the loop ends, so the socket
// can be closed and the accept thread let go.
int runGui(HINSTANCE inst, const GuiInfo& info, bool startHidden,
           void (*onQuit)());

// Add or remove this program from the per-user Run key, which is how Windows
// starts something at sign-in. Deliberately the registry rather than a setting
// in config.json: a copy of Dental Flow that is moved or deleted should not
// leave the office with a startup entry pointing at nothing, and the registry
// is the only place Windows will look.
bool runAtSignInEnabled();
bool setRunAtSignIn(bool on, std::string& error);

// A message box that says what went wrong, for the failures that used to be
// printed to a console nobody will see any more.
void showProblem(const std::string& text);

}  // namespace on
