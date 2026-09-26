//
// The keyboard as a pad, PC style (2026-09-26, the owner's choice): what a key does in every screen that is
// driven by pad buttons, on every platform - a PC stick's keyboard, Windows, a Pi, a USB keyboard on the
// console. Header-only and SDL-free, so the tests can hold the map to what the Button Guide says.
//
//   Arrows = d-pad      Enter = Cross       Backspace / Esc = Circle     Tab = Triangle    Space = Square
//   F1 = Select         F2 = Start          Page Up / Page Down = L1 / R1                 Home / End = L2 / R2
//   F10 = L2+R2 (the launcher's System menu)
//
// On a development machine Esc stays the power off (it is how a dev build is closed) and Space stays Start:
// the dev host's own letter map (X O S T, I J K L, Space, B, Q E 1 2 - Input's translateKeyboardToPad) is
// kept alongside, and it owns Space. The two maps share no other key.
//
#pragma once

#include "input.h"

#include <string>

namespace ableem {
namespace KeyboardMap {

// what a key stands for: a pad button, the L2+R2 chord, or nothing (the key stays a key)
struct Mapped {
    Button button = Button::None;
    bool systemChord = false; // F10: L2 and R2 together
    bool mapped() const { return button != Button::None || systemChord; }
};

// key and code as Event carries them (code is the key's character, ' ' for Space)
inline Mapped toPad(Key key, int code, bool devHost) {
    Mapped m;
    switch (key) {
    case Key::Up:
        m.button = Button::DpadUp;
        break;
    case Key::Down:
        m.button = Button::DpadDown;
        break;
    case Key::Left:
        m.button = Button::DpadLeft;
        break;
    case Key::Right:
        m.button = Button::DpadRight;
        break;
    case Key::Return:
        m.button = Button::Cross;
        break;
    case Key::Backspace:
        m.button = Button::Circle;
        break;
    case Key::Escape:
        if (!devHost)
            m.button = Button::Circle;
        break;
    case Key::Tab:
        m.button = Button::Triangle;
        break;
    case Key::F1:
        m.button = Button::Select;
        break;
    case Key::F2:
        m.button = Button::Start;
        break;
    case Key::PageUp:
        m.button = Button::L1;
        break;
    case Key::PageDown:
        m.button = Button::R1;
        break;
    case Key::Home:
        m.button = Button::L2;
        break;
    case Key::End:
        m.button = Button::R2;
        break;
    case Key::F10:
        m.systemChord = true;
        break;
    case Key::Other:
        if (code == ' ' && !devHost)
            m.button = Button::Square;
        break;
    default:
        break;
    }
    return m;
}

// the key a pad button is on, as the Button Guide names it ("" when none); on a dev host the ones the
// letter map owns
inline std::string keyFor(Button button, bool devHost) {
    switch (button) {
    case Button::Cross:
        return "Enter";
    case Button::Circle:
        return devHost ? "Backspace" : "Esc";
    case Button::Triangle:
        return "Tab";
    case Button::Square:
        return devHost ? "s" : "Space"; // lower case: "S" is the pad's square icon to PanelStyle, a chip reads "S"
    case Button::Select:
        return "F1";
    case Button::Start:
        return "F2";
    case Button::L1:
        return "PgUp";
    case Button::R1:
        return "PgDn";
    case Button::L2:
        return "Home";
    case Button::R2:
        return "End";
    case Button::DpadUp:
        return "Arrow up";
    case Button::DpadDown:
        return "Arrow down";
    case Button::DpadLeft:
        return "Arrow left";
    case Button::DpadRight:
        return "Arrow right";
    default:
        return "";
    }
}

// the key that opens the launcher's System menu (the L2+R2 chord)
inline std::string systemMenuKey() {
    return "F10";
}

} // namespace KeyboardMap
} // namespace ableem
