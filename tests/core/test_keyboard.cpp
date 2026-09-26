//
// The keyboard: KeyboardPresence (is one connected - the sysfs key bitmaps) and KeyboardMap (the PC-style
// keys as pad buttons, and the names the Button Guide shows for them).
//
#include "doctest/doctest.h"
#include "../support/temp_dir.h"

#include <ableem/engine/keyboard_presence.h>
#include <ableem/ui/keyboard_map.h>

using namespace ableem;

namespace {
// an AT keyboard's key bitmap as a 64-bit kernel prints it (a PC)
const char *Keyboard64 = "120013 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1000000 0 0 0 0 0 0 0 0 0 0 "
                         "0 0 0 0 0 0 402000000 3803078f800d001 feffffdfffefffff fffffffffffffffe";
// the same keys from a 32-bit kernel (the console, a 32-bit Pi): the last two words are the low 64 bits
const char *Keyboard32 = "20000 0 4 2000000 3803078 f800d001 feffffdf ffefffff ffffffff fffffffe";
// a pad: BTN_SOUTH.. (0x130..) and nothing else
const char *Pad64 = "7fff000000000000 0 0 0 0";
// the console's power button: KEY_POWER (116) only
const char *PowerButton64 = "10000000000000 0";
const char *PowerButton32 = "100000 0 0 0";
// a number pad: Enter and the digits, no letters
const char *NumPad64 = "100007fe"; // Enter (28) and 1..0 (2..11)
} // namespace

TEST_CASE("a keyboard's bitmap is a keyboard, whichever word size the kernel printed it in") {
    CHECK(KeyboardPresence::capabilitiesLookLikeKeyboard(Keyboard64));
    CHECK(KeyboardPresence::capabilitiesLookLikeKeyboard(Keyboard32));
}

TEST_CASE("a pad, the power button, a number pad and nothing are not") {
    CHECK_FALSE(KeyboardPresence::capabilitiesLookLikeKeyboard(Pad64));
    CHECK_FALSE(KeyboardPresence::capabilitiesLookLikeKeyboard(PowerButton64));
    CHECK_FALSE(KeyboardPresence::capabilitiesLookLikeKeyboard(PowerButton32));
    CHECK_FALSE(KeyboardPresence::capabilitiesLookLikeKeyboard(NumPad64));
    CHECK_FALSE(KeyboardPresence::capabilitiesLookLikeKeyboard(""));
    CHECK_FALSE(KeyboardPresence::capabilitiesLookLikeKeyboard("0\n"));
}

TEST_CASE("the sysfs scan finds a keyboard among the event devices, and only there") {
    TempDir tmp("keyboard");
    tmp.writeFile("event0/device/capabilities/key", std::string(PowerButton32) + "\n");
    tmp.writeFile("event1/device/capabilities/key", std::string(Pad64) + "\n");
    tmp.writeFile("mouse0/device/capabilities/key", std::string(Keyboard64) + "\n"); // not an eventN
    CHECK_FALSE(KeyboardPresence::anyLinuxKeyboard(tmp.path()));
    tmp.writeFile("event2/device/capabilities/key", std::string(Keyboard64) + "\n");
    CHECK(KeyboardPresence::anyLinuxKeyboard(tmp.path()));
    CHECK_FALSE(KeyboardPresence::anyLinuxKeyboard(tmp.path() + "/missing"));
}

TEST_CASE("the PC-style map: every pad button has a key") {
    using KeyboardMap::toPad;
    CHECK(toPad(Key::Up, 0, false).button == Button::DpadUp);
    CHECK(toPad(Key::Down, 0, false).button == Button::DpadDown);
    CHECK(toPad(Key::Left, 0, false).button == Button::DpadLeft);
    CHECK(toPad(Key::Right, 0, false).button == Button::DpadRight);
    CHECK(toPad(Key::Return, 0, false).button == Button::Cross);
    CHECK(toPad(Key::Backspace, 0, false).button == Button::Circle);
    CHECK(toPad(Key::Escape, 0, false).button == Button::Circle);
    CHECK(toPad(Key::Tab, 0, false).button == Button::Triangle);
    CHECK(toPad(Key::Other, ' ', false).button == Button::Square);
    CHECK(toPad(Key::F1, 0, false).button == Button::Select);
    CHECK(toPad(Key::F2, 0, false).button == Button::Start);
    CHECK(toPad(Key::PageUp, 0, false).button == Button::L1);
    CHECK(toPad(Key::PageDown, 0, false).button == Button::R1);
    CHECK(toPad(Key::Home, 0, false).button == Button::L2);
    CHECK(toPad(Key::End, 0, false).button == Button::R2);
    CHECK(toPad(Key::F10, 0, false).systemChord);
    CHECK_FALSE(toPad(Key::Other, 'a', false).mapped()); // a letter stays a letter
    CHECK_FALSE(toPad(Key::Sleep, 0, false).mapped());   // the power button is not touched
    CHECK_FALSE(toPad(Key::Reset, 0, false).mapped());
}

TEST_CASE("on a dev host Esc stays the power off and Space the letter map's Start") {
    using KeyboardMap::toPad;
    CHECK_FALSE(toPad(Key::Escape, 0, true).mapped());
    CHECK_FALSE(toPad(Key::Other, ' ', true).mapped());
    CHECK(toPad(Key::Backspace, 0, true).button == Button::Circle);
    CHECK(toPad(Key::Return, 0, true).button == Button::Cross);
}

TEST_CASE("the Button Guide's names are the keys the map uses") {
    using KeyboardMap::keyFor;
    CHECK(keyFor(Button::Cross, false) == "Enter");
    CHECK(keyFor(Button::Circle, false) == "Esc");
    CHECK(keyFor(Button::Circle, true) == "Backspace");
    CHECK(keyFor(Button::Square, false) == "Space");
    CHECK(keyFor(Button::Square, true) == "s");
    CHECK(keyFor(Button::Select, false) == "F1");
    CHECK(keyFor(Button::Start, false) == "F2");
    CHECK(keyFor(Button::None, false).empty());
    CHECK(KeyboardMap::systemMenuKey() == "F10");
}
