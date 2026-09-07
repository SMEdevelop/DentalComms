// A Windows shortcut that opens Dental Flow in its own window at sign-in.
//
// See shortcut.cpp for why this exists rather than a help page telling people
// to build one by hand. In short: the pop-out button in the page cannot fire
// itself, because browsers only allow window.open() inside a real click - so a
// computer that restarts needs somebody to press it. A browser started with
// --app= makes the same window with nobody pressing anything.
#pragma once
#include <string>

namespace on {

// Builds the shortcut and returns its bytes in `out`, ready to be served as a
// download. `url` is the address staff use, e.g. "http://RECEPTION-PC:3272".
// Returns false and fills `error` when the chosen browser is not installed on
// this computer, or Windows refuses.
//
// Note this reports on the computer running the SERVER. That is the honest
// limit of building it here: the setup page offers both, and says so.
bool buildStartupShortcut(const std::string& url, bool chrome,
                          std::string& out, std::string& error);

// Whether that browser is present, so the setup page can offer the one that
// will actually work rather than a download that fails.
bool browserAvailable(bool chrome);

}  // namespace on
