//
// KeyboardPresence: is a real keyboard connected? SDL has no device list for keyboards, so this asks the
// system: on Linux every input device's key capabilities in sysfs, on Windows the raw input device list.
// What the Button Guide decides by whether to show the keyboard's keys (Input::keyboardPresent() also counts
// a key event seen this session). Cheap enough to ask whenever a screen opens - which is the hot-plug.
//
#pragma once

#include <string>

namespace ableem {
namespace KeyboardPresence {

// Linux: /sys/class/input/eventN/device/capabilities/key - the device's key bitmap as the kernel prints it,
// hex words separated by spaces, the most significant first, leading zero words left out. A keyboard has
// Enter and (nearly) every letter; the console's power/reset buttons, a pad (BTN_*), a remote do not.
// The word size (32 or 64 bits, the kernel's long, which a 32-bit userland on a 64-bit kernel cannot know
// from its own) is told from the text: a keyboard's letters reach bit 50, so on a 64-bit kernel its last
// word is longer than eight digits.
bool capabilitiesLookLikeKeyboard(const std::string &keyBitmap);

// every eventN under sysClassInput (a test's fake tree, or /sys/class/input) checked that way
bool anyLinuxKeyboard(const std::string &sysClassInput = "/sys/class/input");

// this system: the Linux check, the raw input list on Windows (RIM_TYPEKEYBOARD), false elsewhere
bool detect();

} // namespace KeyboardPresence
} // namespace ableem
