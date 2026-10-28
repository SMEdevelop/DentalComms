// The one place Dental Comms's version is declared for the C++ side.
//
// src\app.rc carries the same numbers for the Windows version resource (the
// Details tab in Explorer), and package.bat reads them back out of app.rc to
// name the zip. Change all three together, or change app.rc and let
// package.bat follow.
#pragma once

#define DC_VERSION_MAJOR 0
#define DC_VERSION_MINOR 9
#define DC_VERSION_PATCH 7

// Shown in the startup banner. "beta" is part of the string on purpose: the
// person starting this in an office should be able to see at a glance that it
// is not the finished thing.
#define DC_VERSION_STRING "0.9.7 beta"
