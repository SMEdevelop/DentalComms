// A batch file that makes Dental Comms open by itself at sign-in, built for the
// address the office actually reaches this server on.
//
// See shortcut.cpp for why this is a script rather than the .lnk it used to
// be. In short: Chromium will not save a .lnk at all - it renames it to
// .download - so the shortcut has to be made on the staff computer rather than
// downloaded to it. Running there is also the only honest way to tell which
// browser that computer has, which building it on the server never could.
#pragma once
#include <string>

namespace on {

// Builds the setup script and returns its text in `out`, ready to be served as
// a download. `url` is the address staff use, e.g. "http://RECEPTION-PC:3272";
// `chrome` chooses which browser the script tries first, though it falls back
// to the other rather than giving up.
//
// Returns false and fills `error` when `url` is not an address that can safely
// be put in a batch file - see safeUrl in the implementation.
bool buildStartupScript(const std::string& url, bool chrome, std::string& out,
                        std::string& error);

}  // namespace on
