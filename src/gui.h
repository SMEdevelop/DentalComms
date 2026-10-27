// The server's window and its notification-area icon.
//
// Dental Comms spent its first versions as a console program, which worked but
// asked the office to understand something they should not have to: that a
// black window full of text is load-bearing, and that closing it turns the
// messaging off. Staff close windows. This replaces it with an ordinary small
// window that tucks itself into the notification area instead of stopping.
//
// The window has two jobs. The first time, it walks whoever installed Dental
// Comms through three screens - Welcome, a checklist that fills itself in,
// and the address to give everyone - so nobody needs the README. After that
// it is a status window: the address, and the same checklist kept live, with
// a button beside anything that can be put right from here.
//
// Nothing here knows how to serve a request. The accept loop runs on its own
// thread and this only reports what it is doing, so the window being busy -
// dragged, or a menu open - can never stall a message.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "checks.h"

namespace on {

struct GuiHooks {
    // Called on the UI thread just before the loop ends, so the socket can be
    // closed and the accept thread let go.
    void (*onQuit)() = nullptr;
    // Re-reads the world - moving the listener if the network changed - and
    // says what to show. `deep` adds the firewall check, which takes a few
    // tens of milliseconds and so is not done every few seconds while hidden.
    std::function<Facts(bool deep)> refresh;
    // The welcome has been finished; remember it.
    std::function<void()> markWelcomed;
};

// Runs the window and the message loop. Returns when the user quits.
// `showWelcome` starts the three-screen first run instead of the status view;
// `startHidden` opens straight into the notification area.
int runGui(HINSTANCE inst, const Facts& initial, bool showWelcome,
           bool startHidden, const GuiHooks& hooks);

// One Dental Comms per Windows session. Returns false when another copy
// already holds it - most often one hidden by the clock that someone has just
// double-clicked the icon for again. That used to fail with "another program
// is using port 3272, change the port", which was wrong, and following it
// would have broken every bookmark in the office.
bool claimSingleInstance();

enum class InstanceRequest { Show, Quit };
// Asks the copy that is already running to show its window, or to turn off
// (what the installer needs before it can update the files). True if one was
// found to ask.
bool signalRunningCopy(InstanceRequest r);

// Asks any older Dental Comms or Dental Flow server window on this computer
// to turn off, the same way its tray menu would. Used when an older copy,
// which predates the single-instance check, is holding the port.
bool stopOtherServers();

// A question with big, plain answer buttons (a Windows task dialog). Returns
// the id of the choice made, or 0 if the window was closed.
struct Choice {
    int id;
    std::wstring text;   // first line is the button; a second line explains it
};
int askChoice(const std::wstring& instruction, const std::wstring& content,
              const std::vector<Choice>& choices);

// Add or remove this program from the per-user Run key, which is how Windows
// starts something at sign-in. Deliberately the registry rather than a setting
// in config.json: a copy of Dental Comms that is moved or deleted should not
// leave the office with a startup entry pointing at nothing, and the registry
// is the only place Windows will look.
bool runAtSignInEnabled();
bool setRunAtSignIn(bool on, std::string& error);

// Before the rename to Dental Comms the Run entry was called "DentalFlow" and
// started DentalFlow.exe. Moves one that started the copy in this folder over
// to the new name and this exe, so an upgraded server still starts by itself.
void migrateRunAtSignIn();

// A message box that says what went wrong, for the few failures that happen
// before there is a window to show them in.
void showProblem(const std::string& text);

}  // namespace on
