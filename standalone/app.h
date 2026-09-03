// Who this binary is.
//
// One implementation serves all five standalones, so the three things that differ between them —
// the executable's own name, the window title, and which bundle to load — are supplied by the
// build rather than written five times. CMake compiles standalone/main.cpp once per pedal with
// RPEDALS_APP_EXE, RPEDALS_APP_TITLE and RPEDALS_APP_BUNDLE defined, and main.cpp is the one
// place that turns those into the three symbols below.
//
// They are extern rather than macros because jackclient.cpp, runloop.cpp and midiroute.cpp all
// name the program in their diagnostics, and a message that says "rations-standalone" when the
// user ran rations-delay-standalone sends them looking in the wrong place — five sibling
// applications is the same problem five sibling plug-ins have.
#pragma once

namespace Rations
{

extern const char *const kAppExe;    // "rations-delay-standalone", for diagnostics
extern const char *const kAppTitle;  // "Rations Delay", for the window title
extern const char *const kAppBundle; // "RationsDelay.vst3", the bundle to load

} // namespace Rations
