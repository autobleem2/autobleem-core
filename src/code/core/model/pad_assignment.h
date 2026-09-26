//
// Which PS1 controller port a connected pad lands on.
//
#pragma once

#include <string>

// Both PS1 emulators we ship (pcsx-ab, pcsx-abnxt) assign port 1/2 by ascending SDL joystick
// device-index at the moment they start (no GUID pinning, no saved order - see
// in_sdl2gc_probe()/check_and_reprobe() in each emulator's frontend/libpicofe/in_sdl2gc.c).
// ableem::Input::pads() enumerates in that same ascending-index order, so `index` here is a pad's
// position in that vector (0-based) - what pcsx-ab/abnxt will call port `index + 1`.
//
// Returns the untranslated English label; translate at the call site with _(). `count` is how
// many pads are connected - out-of-range asks (a negative index, or index >= count) get "".
inline std::string psPlayerLabel(int index, int count) {
    if (index < 0 || index >= count)
        return "";
    if (index == 0)
        return "Player 1";
    if (index == 1)
        return "Player 2";
    return "not used by the PS1 emulator";
}
